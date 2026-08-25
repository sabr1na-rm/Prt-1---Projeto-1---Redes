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

typedef enum{
    CMD_MENSAGEM,
    CMD_NOME,
    CMD_QUIT
} Comandos;

typedef struct NoComando{
    Comandos tipo;
    char texto[TAM_BUFFER];
    struct NoComando *prox;
} NoComando;

NoComando *fila_inicio = NULL;
NoComando *fila_fim = NULL;
pthread_mutex_t mutex_fila = PTHREAD_MUTEX_INITIALIZER;

char nome_usuario[TAM_NOME];
pthread_mutex_t mutex_nome = PTHREAD_MUTEX_INITIALIZER;

SOCKET socket_cliente = INVALID_SOCKET;

volatile int ativo = 1;


void obter_horario(char *saida, size_t tam)
{
    time_t agora = time(NULL);
    struct tm *info = localtime(&agora);
    strftime(saida, tam, "%H:%M:%S", info);
}

int enviar_para_cliente(const char *msg)
{
    int r = send(socket_cliente, msg, (int)strlen(msg), 0);
    if(r == SOCKET_ERROR)
    {
        ativo = 0;
        return-1;
    }
    return 0;
}

void enfileirar_comando(Comandos tipo, const char *texto)
{
    NoComando *novo = (NoComando *)malloc(sizeof(NoComando));
    if(!novo)
    {
        return;
    }
    novo->tipo = tipo;
    strncpy(novo->texto, texto, TAM_BUFFER -1);
    novo->texto[TAM_BUFFER - 1] = '\0';
    novo->prox = NULL;

    pthread_mutex_lock(&mutex_fila);
    if(fila_fim == NULL)
    {
        fila_inicio = novo;
        fila_fim = novo;
    }
    else
    {
        fila_fim->prox = novo;
        fila_fim = novo;
    }
    pthread_mutex_unlock(&mutex_fila);
}

NoComando *desenfileirar_comando(void)
{
    NoComando *no;

    pthread_mutex_lock(&mutex_fila);
    no = fila_inicio;
    if(no != NULL)
    {
        fila_inicio = no->prox;
        if(fila_inicio == NULL)
        {
            fila_fim = NULL;
        }
    }
    pthread_mutex_unlock(&mutex_fila);

    return no;
}

void *thread_recebe(void *arg)
{
    char buffer[TAM_BUFFER];

    while(ativo)
    {
        memset(buffer, 0, TAM_BUFFER);

        int n = recv(socket_cliente, buffer, TAM_BUFFER - 1, 0);
        if(n <= 0)
        {
            ativo = 0;
            break;
        }
        buffer[n] = '\0';

        size_t len = strlen(buffer);
        while(len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
        {
            buffer[--len] = '\0';
        }
        if(len == 0)
        {
            continue;
        }
        if(buffer[0] == ':')
        {
            if(strncmp(buffer, ":nome", 6) == 0)
            {
                enfileirar_comando(CMD_NOME, buffer + 6);
            }
            else if(strcmp(buffer, ":quit") == 0)
            {
                enfileirar_comando(CMD_QUIT, "");
            }
        }
        else
        {
            enfileirar_comando(CMD_MENSAGEM, buffer);
        }
    }

    return NULL;
}

void *thread_processa(void *arg)
{
    time_t ultimo_horario_enviado = time(NULL);

    while(ativo)
    {
        NoComando *no;

        while((no = desenfileirar_comando()) != NULL)
        {
            char saida[TAM_BUFFER + TAM_NOME + 32];

            switch(no->tipo)
            {
            case CMD_NOME:
                pthread_mutex_lock(&mutex_fila);
                strncpy(nome_usuario, no->texto, TAM_NOME -1);
                nome_usuario[TAM_NOME -1] = '\0';
                pthread_mutex_unlock(&mutex_fila);
                break;

            case CMD_MENSAGEM:
                snprintf(saida, sizeof(saida), "Voce digitou: %s\n", no->texto);
                enviar_para_cliente(saida);
                break;

            case CMD_QUIT:
                ativo = 0;
                shutdown(socket_cliente, SD_BOTH);
                break;
            }

            free(no);
            if(!ativo)
            {
                break;
            }
        }

        time_t agora = time(NULL);
        if(ativo && difftime(agora, ultimo_horario_enviado) >= INTERVALO_HORARIO)
        {
            char horario[16];
            char saida[64];

            obter_horario(horario, sizeof(horario));
            snprintf(saida, sizeof(saida), "%s: (hora atual do servidor)\n", horario);
            enviar_para_cliente(saida);

            ultimo_horario_enviado = agora;
        }

        Sleep(INTERVALO_SCAN);
    }

    return NULL;
}


int main(void)
{
    WSADATA dados_wsa;
    SOCKET socket_escuta;
    struct sockaddr_in endereco_servidor;
    struct sockaddr_in endereco_cliente;
    int tam_endereco = sizeof(endereco_cliente);

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

    if(listen(socket_escuta, 1) == SOCKET_ERROR)
    {
        printf("Erro no listen. Codigo: %d\n", WSAGetLastError());
        closesocket(socket_escuta);
        WSACleanup();
        return 1;
    }

    printf("Servidor de chat aguardando conexao na porta %d...\n", PORTA);

    socket_cliente = accept(socket_escuta, (struct sockaddr *)&endereco_cliente, &tam_endereco);
    if(socket_cliente == INVALID_SOCKET)
    {
        printf("Erro no accept. Codigo: %d\n", WSAGetLastError());
        closesocket(socket_escuta);
        WSACleanup();
        return 1;
    }

    closesocket(socket_escuta);

    {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &endereco_cliente.sin_addr, ip_str, sizeof(ip_str));
        snprintf(nome_usuario, TAM_NOME, "%s:%d", ip_str, ntohs(endereco_cliente.sin_port));
    }

    printf("Cliente conectado: %s\n", nome_usuario);

    {
        char horario[16];
        char msg1[64];

        obter_horario(horario, sizeof(horario));
        snprintf(msg1, sizeof(msg1), "%s: CONECTADO!\n", horario);
        enviar_para_cliente(msg1);
    }

    pthread_t t1, t2;

    if(pthread_create(&t1, NULL, thread_recebe, NULL) != 0)
    {
        printf("Erro ao criar thread 1.\n");
        closesocket(socket_cliente);
        WSACleanup();
        return 1;
    }

    if(pthread_create(&t2, NULL, thread_processa, NULL) != 0)
    {
        printf("Erro ao criar thread 2.\n");
        closesocket(socket_cliente);
        WSACleanup();
        return 1;
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    closesocket(socket_cliente);
    WSACleanup();

    printf("\nServidor encerrado.\n");
    return 0;
}
