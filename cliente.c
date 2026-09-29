#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORTA 8080
#define TAM_BUFFER 1024

SOCKET socket_cliente;

volatile int ativo = 1;


/* THREAD 1 - Lê o teclado e envia os comandos/mensagens ao servidor */

void *thread_envia(void *arg)
{
    char buffer[TAM_BUFFER];

    while (ativo)
    {
        printf("> ");
        fflush(stdout);

        if (fgets(buffer, TAM_BUFFER, stdin) == NULL)
        {
            ativo = 0;
            shutdown(socket_cliente, SD_BOTH); /* PARTE 2: destrava o recv da thread 2 */
            break;
        }

        if (send(socket_cliente,
                 buffer,
                 (int)strlen(buffer),
                 0) == SOCKET_ERROR)
        {
            printf("\nErro ao enviar mensagem.\n");
            ativo = 0;
            shutdown(socket_cliente, SD_BOTH); /* PARTE 2: destrava o recv da thread 2 */
            break;
        }

        
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
        memset(buffer, 0, TAM_BUFFER);

        int n = recv(socket_cliente,
                     buffer,
                     TAM_BUFFER - 1,
                     0);

        if (n <= 0)
        {
            ativo = 0;
            break;
        }

        buffer[n] = '\0';

        printf("\n%s", buffer);

        printf("> ");
        fflush(stdout);
    }

    return NULL;
}



int main(void)
{
    WSADATA dados_wsa;

    if (WSAStartup(MAKEWORD(2, 2), &dados_wsa) != 0)
    {
        printf("Erro ao inicializar o Winsock.\n");
        return 1;
    }




    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_cliente == INVALID_SOCKET)
    {
        printf("Erro ao criar socket. Codigo: %d\n",
               WSAGetLastError());

        WSACleanup();
        return 1;
    }


    struct sockaddr_in servidor;

    memset(&servidor, 0, sizeof(servidor));

    servidor.sin_family = AF_INET;

    servidor.sin_port = htons(PORTA);

    servidor.sin_addr.s_addr = inet_addr("127.0.0.1");


    /* CONECTA AO SERVIDOR */

    if (connect(socket_cliente,
                (struct sockaddr *)&servidor,
                sizeof(servidor)) == SOCKET_ERROR)
    {
        printf("Erro ao conectar ao servidor. Codigo: %d\n",
               WSAGetLastError());

        closesocket(socket_cliente);
        WSACleanup();

        return 1;
    }

    printf("Conectado ao servidor!\n");



    pthread_t t1;
    pthread_t t2;

    /* Thread 1 -> teclado e envio */
    if (pthread_create(&t1,
                       NULL,
                       thread_envia,
                       NULL) != 0)
    {
        printf("Erro ao criar Thread 1.\n");

        closesocket(socket_cliente);
        WSACleanup();

        return 1;
    }

    /* Thread 2 -> recebe mensagens */
    if (pthread_create(&t2,
                       NULL,
                       thread_recebe,
                       NULL) != 0)
    {
        printf("Erro ao criar Thread 2.\n");

        ativo = 0;

        closesocket(socket_cliente);
        WSACleanup();

        return 1;
    }


    pthread_join(t2, NULL);


    closesocket(socket_cliente);

    WSACleanup();

    printf("\nCliente encerrado.\n");

    return 0;
}
