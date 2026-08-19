/*
   pra compilar:

   1) Project -> Build options -> Linker settings -> Add, e digita (sem
      "-l" na frente, o Code::Blocks completa sozinho):
           pthread
           ws2_32
   2) O compilador do projeto tem que ser "GNU GCC Compiler" (MinGW).
   ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORTA        8080  /* numero da porta que o servidor vai usar     */
#define TAM_BUFFER   1024  /* quantos caracteres cabem numa mensagem      */
#define TAM_FILA_MAX 32    /* quantos comandos a fila aguenta guardar     */
#define TAM_NOME     64    /* quantos caracteres cabem no nome do usuario */


/* tipos de comando que o cliente pode mandar pro servidor */
typedef enum { CMD_MSG, CMD_NOME, CMD_QUIT } TipoComando;

/*  comando guardado na fila (a "area de memoria compartilhada") */
typedef struct {
    TipoComando tipo;
    char conteudo[TAM_BUFFER];
} Comando;


typedef struct {
    Comando itens[TAM_FILA_MAX];
    int inicio;               /* indice do proximo a ser removido        */
    int fim;                  /* indice onde o proximo vai ser inserido  */
    int total;                /* quantos itens tem na fila agora         */
    pthread_mutex_t mutex;    /* trava o acesso pq as 2 threads mexem aqui */
} FilaComandos;


typedef struct {

    SOCKET socket_cliente;  /* o que liga o servidor no cliente */
    char   nome[TAM_NOME];
    char   ip[INET_ADDRSTRLEN];
    int    porta;
    volatile int ativo;   /* 1 = conexao ok, 0 = precisa encerrar.
                              "volatile" avisa o compilador que essa
                              variavel pode mudar por causa de OUTRA
                              thread, entao ele nao pode "otimizar" e
                              assumir que o valor nunca muda sozinho */
    FilaComandos fila;
} Cliente;


/* pega a hora atual e formata como HH:MM:SS */
void obter_horario(char *buffer, size_t tamanho) {
    time_t agora = time(NULL);
    struct tm *tm_info = localtime(&agora);
    strftime(buffer, tamanho, "%H:%M:%S", tm_info);
}

/* prepara a fila antes de usar (zera os contadores e inicia o mutex) */
void fila_inicializar(FilaComandos *f) {
    f->inicio = 0;
    f->fim = 0;
    f->total = 0;
    pthread_mutex_init(&f->mutex, NULL);
}


int fila_inserir(FilaComandos *f, TipoComando tipo, const char *conteudo) {
    int ok = 0;
    pthread_mutex_lock(&f->mutex);
    if (f->total < TAM_FILA_MAX) {
        f->itens[f->fim].tipo = tipo;
        strncpy(f->itens[f->fim].conteudo, conteudo, TAM_BUFFER - 1);
        f->itens[f->fim].conteudo[TAM_BUFFER - 1] = '\0';
        f->fim = (f->fim + 1) % TAM_FILA_MAX; /* anda 1 posicao, voltando ao inicio se precisar */
        f->total++;
        ok = 1;
    }
    pthread_mutex_unlock(&f->mutex);
    return ok;
}

/* remove o comando mais antigo da fila  chamada pela Thread 2.
   devolve 1 se pegou algo (preenchendo *saida), 0 se a fila estava vazia */
int fila_remover(FilaComandos *f, Comando *saida) {
    int ok = 0;
    pthread_mutex_lock(&f->mutex);
    if (f->total > 0) {
        *saida = f->itens[f->inicio];
        f->inicio = (f->inicio + 1) % TAM_FILA_MAX;
        f->total--;
        ok = 1;
    }
    pthread_mutex_unlock(&f->mutex);
    return ok;
}


void *thread_recebe_comandos(void *arg) {
    Cliente *c = (Cliente *) arg; /* pega o ponteiro do cliente que foi passado no pthread_create */
    char buffer[TAM_BUFFER];

    while (c->ativo) {
        memset(buffer, 0, TAM_BUFFER);
        int n = recv(c->socket_cliente, buffer, TAM_BUFFER - 1, 0);

        if (n <= 0) {
            fila_inserir(&c->fila, CMD_QUIT, "");
            break;
        }

        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        if (buffer[0] == ':') {

            if (strncmp(buffer, ":nome ", 6) == 0) {
                fila_inserir(&c->fila, CMD_NOME, buffer + 6);
            } else if (strcmp(buffer, ":quit") == 0) {
                fila_inserir(&c->fila, CMD_QUIT, "");
                break;
            }

        } else {

            fila_inserir(&c->fila, CMD_MSG, buffer);
        }
    }
    return NULL;
}


void *thread_processa_e_envia_horario(void *arg) {
    Cliente *c = (Cliente *) arg;
    char horario[16];
    char saida[TAM_BUFFER];
    time_t ultimo_envio = time(NULL);

    while (c->ativo) {
        Comando cmd;
        while (fila_remover(&c->fila, &cmd)) {
            obter_horario(horario, sizeof(horario));

            switch (cmd.tipo) {
                case CMD_NOME:
                    strncpy(c->nome, cmd.conteudo, TAM_NOME - 1);
                    c->nome[TAM_NOME - 1] = '\0';
                    snprintf(saida, TAM_BUFFER, "%s: nome alterado para %s\n",
                             horario, c->nome);
                    send(c->socket_cliente, saida, (int) strlen(saida), 0);
                    break;

                case CMD_MSG:

                    snprintf(saida, TAM_BUFFER, "Voce digitou: %.900s\n", cmd.conteudo);
                    send(c->socket_cliente, saida, (int) strlen(saida), 0);

                    break;

                case CMD_QUIT:
                    snprintf(saida, TAM_BUFFER, "%s: Desconectando...\n", horario);
                    send(c->socket_cliente, saida, (int) strlen(saida), 0);
                    c->ativo = 0;
                    break;
            }
        }


        time_t agora = time(NULL);
        if (c->ativo && difftime(agora, ultimo_envio) >= 60) {
            obter_horario(horario, sizeof(horario));
            snprintf(saida, TAM_BUFFER, "[Servidor] Data/Hora: %s\n", horario);
            send(c->socket_cliente, saida, (int) strlen(saida), 0);
            ultimo_envio = agora;
        }

        Sleep(200);
    }

    closesocket(c->socket_cliente);

int main(void) {
    Cliente cliente;


    WSADATA dados_wsa;

    if (WSAStartup(MAKEWORD(2, 2), &dados_wsa) != 0) {
        printf("Erro ao inicializar o Winsock.\n");
        return 1;
    }

 /* cria o socket do servidor:
       - AF_INET     = vamos usar enderecos IPv4
       - SOCK_STREAM = conexao do tipo TCP (confiavel, com ordem garantida)
       O socket criado e como um "aparelho de telefone" ainda sem numero */
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        printf("Erro no socket. Codigo: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &opt, sizeof(opt));

    /* monta o endereco e da bind */
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORTA);

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) == SOCKET_ERROR) {
        printf("Erro no bind. Codigo: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    /*  listen - fila de espera de 1*/
    if (listen(server_fd, 1) == SOCKET_ERROR) {
        printf("Erro no listen. Codigo: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    printf("Servidor aguardando conexao na porta %d...\n", PORTA);


    socklen_t addrlen = sizeof(address);
    SOCKET novo_socket = accept(server_fd, (struct sockaddr *) &address, &addrlen);
    if (novo_socket == INVALID_SOCKET) {
        printf("Erro no accept. Codigo: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    cliente.socket_cliente = novo_socket;
    cliente.ativo = 1;
    inet_ntop(AF_INET, &address.sin_addr, cliente.ip, INET_ADDRSTRLEN);
    cliente.porta = ntohs(address.sin_port);

    snprintf(cliente.nome, TAM_NOME, "%s:%d", cliente.ip, cliente.porta);
    fila_inicializar(&cliente.fila);

    printf("Cliente conectado: %s\n", cliente.nome);

    char horario[16];
    obter_horario(horario, sizeof(horario));
    char msg1[TAM_BUFFER];
    snprintf(msg1, TAM_BUFFER, "%s: CONECTADO!!\n", horario);
    send(cliente.socket_cliente, msg1, (int) strlen(msg1), 0);


    pthread_t t1, t2;
    pthread_create(&t1, NULL, thread_recebe_comandos, &cliente);
    pthread_create(&t2, NULL, thread_processa_e_envia_horario, &cliente);


    pthread_join(t1, NULL);
    pthread_join(t2, NULL);


    closesocket(server_fd);
    WSACleanup();
    printf("Servidor encerrado.\n");
    return 0;
}
}
