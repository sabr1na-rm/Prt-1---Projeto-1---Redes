#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORTA 8080
#define TAM_BUFFER 1024
#define TAM_NOME 64
#define INTERVALO_HORARIO 60
#define INTERVALO_SCAN 100

typedef enum {
    CMD_MENSAGEM,
    CMD_NOME,
    CMD_QUIT
} Comandos;

typedef struct NoComando {
    Comandos tipo;
    char texto[TAM_BUFFER];
    struct NoComando *prox;
} NoComando;

/* PARTE 2: Estrutura independente para cada cliente */
typedef struct Cliente {
    SOCKET socket;
    char nome[TAM_NOME];
    NoComando *fila_inicio;
    NoComando *fila_fim;
    pthread_mutex_t mutex_fila;
    volatile int ativo;
} Cliente;

/* PARTE 2: Memória compartilhada entre todos os clientes */
int max_clientes;
int clientes_conectados = 0;
pthread_mutex_t mutex_contador = PTHREAD_MUTEX_INITIALIZER;
Cliente **lista_clientes; /* Array global para armazenar os clientes conectados */

void obter_horario(char *saida, size_t tam)
{
    time_t agora = time(NULL);
    struct tm *info = localtime(&agora);
    strftime(saida, tam, "%H:%M:%S", info);
}

int enviar_para_cliente(Cliente *c, const char *msg)
{
    int r = send(c->socket, msg, (int)strlen(msg), 0);
    if(r == SOCKET_ERROR)
    {
        c->ativo = 0;
        shutdown(c->socket, SD_BOTH); /* PARTE 2: destrava o recv da thread 1 */
        return -1;
    }
    return 0;
}

/* PARTE 2: envia a mensagem recebida para todos os outros usuários ativos */
void broadcast_mensagem(Cliente *remetente, const char *texto)
{
    char horario[16];
    char msg_publica[TAM_BUFFER + TAM_NOME + 32];

    obter_horario(horario, sizeof(horario));
    snprintf(msg_publica, sizeof(msg_publica), "%s (%s): %s\n", remetente->nome, horario, texto);

    pthread_mutex_lock(&mutex_contador);
    for (int i = 0; i < max_clientes; i++) {
        /* Posição ocupada, cliente ativo e não é o próprio remetente */
        if (lista_clientes[i] != NULL && lista_clientes[i]->ativo && lista_clientes[i] != remetente) {
            enviar_para_cliente(lista_clientes[i], msg_publica);
        }
    }
    pthread_mutex_unlock(&mutex_contador);
}

void enfileirar_comando(Cliente *c, Comandos tipo, const char *texto)
{
    NoComando *novo = (NoComando *)malloc(sizeof(NoComando));
    if(!novo) return;

    novo->tipo = tipo;
    strncpy(novo->texto, texto, TAM_BUFFER -1);
    novo->texto[TAM_BUFFER - 1] = '\0';
    novo->prox = NULL;

    pthread_mutex_lock(&c->mutex_fila);
    if(c->fila_fim == NULL)
    {
        c->fila_inicio = novo;
        c->fila_fim = novo;
    }
    else
    {
        c->fila_fim->prox = novo;
        c->fila_fim = novo;
    }
    pthread_mutex_unlock(&c->mutex_fila);
}

NoComando *desenfileirar_comando(Cliente *c)
{
    NoComando *no;
    pthread_mutex_lock(&c->mutex_fila);
    no = c->fila_inicio;
    if(no != NULL)
    {
        c->fila_inicio = no->prox;
        if(c->fila_inicio == NULL)
        {
            c->fila_fim = NULL;
        }
    }
    pthread_mutex_unlock(&c->mutex_fila);
    return no;
}

void *thread_recebe(void *arg)
{
    Cliente *c = (Cliente *)arg;
    char buffer[TAM_BUFFER];

    while(c->ativo)
    {
        memset(buffer, 0, TAM_BUFFER);
        int n = recv(c->socket, buffer, TAM_BUFFER - 1, 0);

        if(n <= 0)
        {
            c->ativo = 0;
            break;
        }

        buffer[n] = '\0';
        size_t len = strlen(buffer);
        while(len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
        {
            buffer[--len] = '\0';
        }

        if(len == 0) continue;

        if(buffer[0] == ':')
        {
            /* PARTE 2: compara 6 caracteres (":nome ") */
            if(strncmp(buffer, ":nome ", 6) == 0)
            {
                enfileirar_comando(c, CMD_NOME, buffer + 6);
            }
            else if(strcmp(buffer, ":quit") == 0)
            {
                enfileirar_comando(c, CMD_QUIT, "");
            }
        }
        else
        {
            enfileirar_comando(c, CMD_MENSAGEM, buffer);
        }
    }
    return NULL;
}

void *thread_processa(void *arg)
{
    Cliente *c = (Cliente *)arg;
    time_t ultimo_horario_enviado = time(NULL);

    while(c->ativo)
    {
        NoComando *no;
        while((no = desenfileirar_comando(c)) != NULL)
        {
            char saida[TAM_BUFFER + TAM_NOME + 32];
            switch(no->tipo)
            {
                case CMD_NOME:
                    strncpy(c->nome, no->texto, TAM_NOME -1);
                    c->nome[TAM_NOME -1] = '\0';
                    break;

                case CMD_MENSAGEM:
                    /* Eco para quem enviou e broadcast para os outros */
                    snprintf(saida, sizeof(saida), "Voce digitou: %s\n", no->texto);
                    enviar_para_cliente(c, saida);
                    broadcast_mensagem(c, no->texto);
                    break;

                case CMD_QUIT:
                    c->ativo = 0;
                    shutdown(c->socket, SD_BOTH); /* Destrava o recv da thread 1 */
                    break;
            }
            free(no);
            if(!c->ativo) break;
        }

        time_t agora = time(NULL);
        if(c->ativo && difftime(agora, ultimo_horario_enviado) >= INTERVALO_HORARIO)
        {
            char horario[16];
            char saida[64];
            obter_horario(horario, sizeof(horario));
            snprintf(saida, sizeof(saida), "%s: (hora atual do servidor)\n", horario);
            enviar_para_cliente(c, saida);
            ultimo_horario_enviado = agora;
        }
        Sleep(INTERVALO_SCAN);
    }
    return NULL;
}

/* PARTE 2: thread de trabalho, uma por cliente */
void *thread_trabalho(void *arg)
{
    Cliente *c = (Cliente *)arg;
    pthread_t t1, t2;
    char horario[16];
    char msg1[64];

    obter_horario(horario, sizeof(horario));
    snprintf(msg1, sizeof(msg1), "%s: CONECTADO!\n", horario);
    enviar_para_cliente(c, msg1);

    if(pthread_create(&t1, NULL, thread_recebe, c) == 0)
    {
        if(pthread_create(&t2, NULL, thread_processa, c) == 0)
        {
            pthread_join(t2, NULL);
        }
        else
        {
            c->ativo = 0;
            shutdown(c->socket, SD_BOTH);
        }
        pthread_join(t1, NULL);
    }

    /* PARTE 2: primeiro tira o cliente da lista e libera a vaga,
       para nenhum broadcast usar um socket que já foi fechado */
    pthread_mutex_lock(&mutex_contador);
    for (int i = 0; i < max_clientes; i++) {
        if (lista_clientes[i] == c) {
            lista_clientes[i] = NULL;
            break;
        }
    }
    clientes_conectados--;
    pthread_mutex_unlock(&mutex_contador);

    /* Fecha a conexão TCP e desaloca os dados do cliente */
    closesocket(c->socket);

    NoComando *no;
    while((no = desenfileirar_comando(c)) != NULL)
    {
        free(no);
    }

    printf("Cliente desconectado: %s\n", c->nome);
    pthread_mutex_destroy(&c->mutex_fila);
    free(c);
    return NULL;
}

int main(int argc, char *argv[])
{
    WSADATA dados_wsa;
    SOCKET socket_escuta;
    struct sockaddr_in endereco_servidor;
    struct sockaddr_in endereco_cliente;
    int tam_endereco;

    /* PARTE 2: limite de clientes vem da linha de comando */
    if(argc != 2 || atoi(argv[1]) <= 0)
    {
        printf("Uso: %s <max_clientes>\n", argv[0]);
        return 1;
    }

    max_clientes = atoi(argv[1]);
    lista_clientes = (Cliente **)calloc(max_clientes, sizeof(Cliente *));

    if(WSAStartup(MAKEWORD(2, 2), &dados_wsa) != 0)
    {
        printf("Erro ao inicializar o Winsock.\n");
        return 1;
    }

    socket_escuta = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_escuta == INVALID_SOCKET)
    {
        printf("Erro ao criar socket. Codigo: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    memset(&endereco_servidor, 0, sizeof(endereco_servidor));
    endereco_servidor.sin_family = AF_INET;
    endereco_servidor.sin_addr.s_addr = INADDR_ANY;
    endereco_servidor.sin_port = htons(PORTA);

    if(bind(socket_escuta, (struct sockaddr *)&endereco_servidor, sizeof(endereco_servidor)) == SOCKET_ERROR)
    {
        printf("Erro no bind. Codigo: %d\n", WSAGetLastError());
        closesocket(socket_escuta);
        WSACleanup();
        return 1;
    }

    if(listen(socket_escuta, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("Erro no listen. Codigo: %d\n", WSAGetLastError());
        closesocket(socket_escuta);
        WSACleanup();
        return 1;
    }

    printf("Servidor de chat aguardando conexoes na porta %d (max %d clientes)...\n", PORTA, max_clientes);

    /* PARTE 2: o main só aceita conexões; quem atende é a thread de trabalho */
    while(1)
    {
        tam_endereco = sizeof(endereco_cliente);
        SOCKET novo = accept(socket_escuta, (struct sockaddr *)&endereco_cliente, &tam_endereco);

        if(novo == INVALID_SOCKET)
        {
            printf("Erro no accept. Codigo: %d\n", WSAGetLastError());
            continue;
        }

        /* Procura posição livre e verifica limite */
        pthread_mutex_lock(&mutex_contador);
        int indice_livre = -1;
        for (int i = 0; i < max_clientes; i++) {
            if (lista_clientes[i] == NULL) {
                indice_livre = i;
                break;
            }
        }

        if(indice_livre == -1)
        {
            pthread_mutex_unlock(&mutex_contador);
            char horario[16];
            char msg[96];
            obter_horario(horario, sizeof(horario));
            snprintf(msg, sizeof(msg), "%s: SERVIDOR CHEIO, limite de clientes atingido.\n", horario);
            send(novo, msg, (int)strlen(msg), 0);
            closesocket(novo);
            continue;
        }

        Cliente *c = (Cliente *)calloc(1, sizeof(Cliente));
        if(!c)
        {
            pthread_mutex_unlock(&mutex_contador);
            closesocket(novo);
            continue;
        }

        c->socket = novo;
        c->ativo = 1;
        pthread_mutex_init(&c->mutex_fila, NULL);
        lista_clientes[indice_livre] = c;
        clientes_conectados++;
        pthread_mutex_unlock(&mutex_contador);

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &endereco_cliente.sin_addr, ip_str, sizeof(ip_str));
        snprintf(c->nome, TAM_NOME, "%s:%d", ip_str, ntohs(endereco_cliente.sin_port));

        printf("Cliente conectado: %s\n", c->nome);

        pthread_t tt;
        if(pthread_create(&tt, NULL, thread_trabalho, c) != 0)
        {
            printf("Erro ao criar thread de trabalho.\n");

            pthread_mutex_lock(&mutex_contador);
            lista_clientes[indice_livre] = NULL;
            clientes_conectados--;
            pthread_mutex_unlock(&mutex_contador);

            closesocket(c->socket);
            pthread_mutex_destroy(&c->mutex_fila);
            free(c);
            continue;
        }
        pthread_detach(tt);
    }

    closesocket(socket_escuta);
    WSACleanup();
    free(lista_clientes);
    return 0;
}
