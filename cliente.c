#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORTA 8080
#define TAM_BUFFER 1024

/* Socket usado para conversar com o servidor */
SOCKET socket_cliente;

/* 1 = cliente funcionando e 0 = cliente deve encerrar*/
volatile int ativo = 1;


/* THREAD 1 - Lê o teclado e envia os comandos/mensagens ao servidor */

void *thread_envia(void *arg)
{
    char buffer[TAM_BUFFER];

    while (ativo)
    {
        printf("> ");
        fflush(stdout);

        /* Lê o que o usuário digitou */
        if (fgets(buffer, TAM_BUFFER, stdin) == NULL)
        {
            ativo = 0;
            break;
        }

        /* Envia o texto para o servidor */
        if (send(socket_cliente,
                 buffer,
                 (int)strlen(buffer),
                 0) == SOCKET_ERROR)
        {
            printf("\nErro ao enviar mensagem.\n");
            ativo = 0;
            break;
        }

        /*
           Se o usuário digitou :quit, podemos encerrar a thread de envio.*/
        if (strcmp(buffer, ":quit\n") == 0 ||
            strcmp(buffer, ":quit") == 0)
        {
            break;
        }
    }

    return NULL;
}


/* THREAD 2 - Recebe mensagens do servidor e mostra na tela */

void *thread_recebe(void *arg)
{
    char buffer[TAM_BUFFER];

    while (ativo)
    {
        /* Limpa o buffer antes de receber uma nova mensagem */
        memset(buffer, 0, TAM_BUFFER);

        /* Espera uma mensagem chegar do servidor */
        int n = recv(socket_cliente,
                     buffer,
                     TAM_BUFFER - 1,
                     0);

        /*n <= 0 significa que a conexão foi encerrada ou aconteceu algum erro.*/
        if (n <= 0)
        {
            ativo = 0;
            break;
        }

        /* Garante que a mensagem termine com '\0' */
        buffer[n] = '\0';

        /* Mostra a mensagem recebida */
        printf("\n%s", buffer);

        printf("> ");
        fflush(stdout);
    }

    return NULL;
}


/* MAIN */

int main(void)
{
    WSADATA dados_wsa;

    /* Inicializa o Winsock */
    if (WSAStartup(MAKEWORD(2, 2), &dados_wsa) != 0)
    {
        printf("Erro ao inicializar o Winsock.\n");
        return 1;
    }


    /* CRIA O SOCKET DO CLIENTE
      AF_INET     -> IPv4 e SOCK_STREAM -> TCP */

    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_cliente == INVALID_SOCKET)
    {
        printf("Erro ao criar socket. Codigo: %d\n",
               WSAGetLastError());

        WSACleanup();
        return 1;
    }
