/**
 * Server code for project 1
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>

//Constants
#define ROWS 3
#define COLUMNS 3
#define MAX_MESSSAGE_SIZE 1000
#define MESSAGE_SIZE 1000
#define MIN_MESSAGE_SIZE 1000
#define EARLIEST_VERSION 7
#define VERSION 8
#define TIMEOUT 10
#define PACKET_RETRIES 3
#define BLOCKING_READ_TIME 1
#define MAX_NUMBER_OF_ACTIVE_GAMES 3

//Flags and Codes
#define GAME_IN_PROGRESS 0
#define GAME_COMPLETE 1
#define GAME_ERROR 2
#define SERVER_PLAYER 1
#define CLIENT_PLAYER 2
#define DRAW 1
#define CLIENT_WIN 2
#define SERVER_WIN 3
#define TCP_FLAGS 0
#define DATAGRAM_FLAGS 0
#define NEW_GAME_COMMAND 0
#define MOVE_COMMAND 1
#define END_GAME_COMMAND 2
#define RECONNECT_COMMAND 3
#define ERROR_OUT_OF_RESOURCES 1
#define ERROR_MALFORMED_REQUEST 2
#define ERROR_SERVER_SHUTDOWN 3
#define ERROR_TIMEOUT 4
#define ERROR_RETRY 5

//Multicast defines
#define MULTICAST_IP "239.0.0.7"
#define MULTICAST_PORT 1818

//Debug defines
#define SENT 1
#define RECEIVED 2
#define ORIGINAL 1
#define REPEAT 2
#define DEBUG_MODE 0 //Switch to 0 to disable packet output

/**
 * Struct for a tictactoe game.
 * active: 0 if game inactive (junk); 1 if active game.
 * socket: The client that is playing the game.
 * board: The game board.
 * timeLastMessage: The time of the last move made by client.
 */
struct tttGame
{

    unsigned char active;
    struct sockaddr_in address;
    char board[ROWS][COLUMNS];
    unsigned char lastMessage[MESSAGE_SIZE];
    //TODO: Make sure this wraps properly
    unsigned char sequenceNumber;
    int connectedSocket;
};

//Array of tttGames
struct tttGame *tttGames;

//Global variables for shutdown
int globTCPSocket;
int globMulticastSocket;
unsigned short globServerPort;

//Function declarations
int verifyArgs(int iCount);
void initTCPSocket(char *strPort);
long convertPort(char *strPort);
int recvMessage(unsigned char messageBuffer[MESSAGE_SIZE], int gameNumber);
void sendMessage(int command, int move, int complete, int completeDescriptor, int gameNumber, unsigned char sequenceNumber, int connectedSocket, unsigned char messageStore[MESSAGE_SIZE]);
void initSharedState(char board[ROWS][COLUMNS]);
int placeMove(char board[ROWS][COLUMNS], int move, int player);
int checkWin(char board[ROWS][COLUMNS], int player);
int placeServerMove(char board[ROWS][COLUMNS]);
void closeSockets();
void allocateGame(int connectedSocket, struct sockaddr_in clientAddress);
void startGame(int gameNumber, unsigned char clientSequenceNum);
int findGameByAddress(struct sockaddr_in address);
int findGameBySocket(int connectedSocket);
void handleMove(int activeGame, int command, int clientGameNum, int move, int clientComplete, int clientCompleteDescriptor, unsigned char clientSequenceNum);
int checkTimeout(struct tttGame session);
void initGamesArray();
void timeoutGames();
void sendPacket(unsigned char packet[MESSAGE_SIZE], int connectedSocket);
void handleEndgame(int gameNumber, int win, int complete, int clientComplete, int clientCompleteDescriptor, unsigned char clientSequenceNum);
void setupSocketSets(fd_set *readSet, fd_set *exceptSet);
void onSelect(fd_set *readSet, fd_set *exceptSet);
void acceptClient();
void debugPacket(unsigned char buf[MAX_MESSSAGE_SIZE], int sentOrReceived, int repeatOrNot);
void initMulticastSocket();
void handleMulticast();
void reconnectGame(int activeGame, unsigned char boardBytes[9]);
void handleMoveAfterPlaced(struct tttGame *clientGame, int clientComplete, int activeGame, int clientCompleteDescriptor, unsigned char clientSequenceNum, int clientGameNum);
void print_board(char board[ROWS][COLUMNS]);

/**
 * Starting point for program.
 * @param argc: Number of args (number of elements in argv).
 * @param *argv[]: Array of args.
 * @retval 0; exit(-1) if error.
 */
int main(int argc, char *argv[])
{

    //Verify correct usage
    verifyArgs(argc);

    //Initialize datagram socket
    initTCPSocket(argv[1]);
    initMulticastSocket();

    //Initialize array of games
    initGamesArray();

    //Print info
    printf("Protocol Version: %d\n", VERSION);
    printf("Setup Success, Listening...\n\n");

    //Declare set of socket descriptors for read and exceptions
    fd_set socketFdReadSet;
    fd_set socketFdExceptSet;

    //Recieve messages in loop
    while (1)
    {
        //Setup timeout for select()
        struct timeval tv;
        tv.tv_sec = BLOCKING_READ_TIME;
        tv.tv_usec = 0;

        //Add all socket descriptors to sets
        setupSocketSets(&socketFdReadSet, &socketFdExceptSet);

        //Select ready sockets
        int selectResult = select(globMulticastSocket + MAX_NUMBER_OF_ACTIVE_GAMES + 1, &socketFdReadSet, NULL, &socketFdExceptSet, &tv);
        if (selectResult == -1)
        {
            perror("Error: Select failed");
            exit(-1);
        }
        else if (select > 0)
        {
            //Something is ready
            onSelect(&socketFdReadSet, &socketFdExceptSet);
        }
        //printf("Waiting on clients")
    }

    //Execution never gets here
    return 0;
}

/**
 * Verifies there are the correct number of command-line args.
 * @param iCount: Number of args passed.
 * @retval 0 success; exit(-1) if error.
 */
int verifyArgs(int iCount)
{
    if (iCount != 2)
    {
        perror("Error: Incorrect number of args. Consult readme for usage.");
        exit(-1);
    }
    else
    {
        return 0;
    }
}

/**
 * Opens and binds socket.
 * @param *strPort: The port to bind to.
 * @retval None; exit(-1) if error.
 */
void initTCPSocket(char *strPort)
{
    globServerPort = convertPort(strPort);

    struct sockaddr_in socketAddressServer;
    socketAddressServer.sin_family = AF_INET;
    socketAddressServer.sin_port = htons(globServerPort);
    socketAddressServer.sin_addr.s_addr = INADDR_ANY;

    globTCPSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (globTCPSocket == 0)
    {
        perror("Error: Problem opening socket");
        exit(-1);
    }

    int bindSuccess = bind(globTCPSocket, (struct sockaddr *)&socketAddressServer, sizeof(socketAddressServer));

    if (bindSuccess != 0)
    {
        perror("Error: Problem binding socket");
        close(globTCPSocket);
        exit(-1);
    }

    //Start listening
    int listenSuccess = listen(globTCPSocket, 1);

    if (listenSuccess != 0)
    {
        perror("Error: Problem starting socket listen");
        close(globTCPSocket);
        exit(-1);
    }
}

/**
 * Parses the passed in string to a long.
 * @param *strPort: the string to parse.
 * @retval Port number to use; exit(-1) if error.
 */
long convertPort(char *strPort)
{
    char *strEndOfParse;

    long lResult = strtol(strPort, &strEndOfParse, 10);
    if (lResult == 0)
    {
        perror("Error: Problem converting port number");
        exit(-1);
    }

    if ((*strEndOfParse != '\0') && (*strEndOfParse != '\n'))
    {
        perror("Port arg parsing error: invalid character. Please use only ASCII numeric characters");
        exit(-1);
    }

    //Conversion success, validate range conforms to port range.
    if (lResult < 0)
    {
        perror("Error: Port out of range. Min port number is 0.");
        exit(-1);
    }
    if (lResult > 65535)
    {
        perror("Error: Port out of range. Max port number is 65535.");
        exit(-1);
    }

    return lResult;
}

void initMulticastSocket()
{
    struct sockaddr_in multicastAddr;
    multicastAddr.sin_family = AF_INET;
    multicastAddr.sin_port = htons(MULTICAST_PORT);
    multicastAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    globMulticastSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if (globMulticastSocket == 0)
    {
        perror("Error: Problem opening socket");
        close(globTCPSocket);
        exit(-1);
    }

    int bindSuccess = bind(globMulticastSocket, (struct sockaddr *)&multicastAddr, sizeof(multicastAddr));

    if (bindSuccess != 0)
    {
        perror("Error: Problem binding socket");
        close(globTCPSocket);
        close(globMulticastSocket);
        exit(-1);
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_IP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(globMulticastSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1)
    {
        perror("Error: Problem setting multicast socket options");
        close(globTCPSocket);
        close(globMulticastSocket);
        exit(-1);
    }

    //Setup blocking read timeout
    //https://stackoverflow.com/questions/13547721/udp-socket-set-timeout
    struct timeval tv;
    tv.tv_sec = BLOCKING_READ_TIME;
    tv.tv_usec = 0;
    if (setsockopt(globMulticastSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("Error: Problem adding timeout to socket");
    }
}

/**
 * Receives a message from socket and validates it.
 * @param  messageBuffer[MESSAGE_SIZE]: The buffer to fill with the message.
 * @param  *clientAddress: The address of the client who sent the message.
 * @param  *clientAddressLength: The size of the client address.
 * @retval 1 if message valid; 0 if message invalid; exit(-1) if error.
 */
int recvMessage(unsigned char messageBuffer[MESSAGE_SIZE], int gameNumber)
{

    //Assume read happens all at once

    unsigned char *recvPointer = messageBuffer;

    int bytesRead = recv(tttGames[gameNumber].connectedSocket, recvPointer, MESSAGE_SIZE, TCP_FLAGS);

    if (bytesRead < 0)
    {
        printf("--- ERROR - Client %d - Socket exception thrown, Closing game. Error: ", gameNumber);
        perror("");
        close(tttGames[gameNumber].connectedSocket);
        tttGames[gameNumber].active = 0;
        return 0;
    }
    if (bytesRead == 0)
    {
        printf("--- ERROR - Client %d - Client socket closed unexpectedly, Closing game\n", gameNumber);
        close(tttGames[gameNumber].connectedSocket);
        tttGames[gameNumber].active = 0;
        return 0;
    }
    if (bytesRead < MIN_MESSAGE_SIZE)
    {
        printf("--- ERROR - Client %d - Message from client too short, Closing game\n", gameNumber);
        unsigned char messageStore[MESSAGE_SIZE];
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, -1, 0, tttGames[gameNumber].connectedSocket, messageStore);
        close(tttGames[gameNumber].connectedSocket);
        tttGames[gameNumber].active = 0;
        return 0;
    }

    if (!(messageBuffer[0] >= EARLIEST_VERSION))
    {
        printf("--- ERROR - Client %d - Client using incompatible protocol version\n", gameNumber);
        unsigned char messageStore[MESSAGE_SIZE];
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, -1, 0, tttGames[gameNumber].connectedSocket, messageStore);
        close(tttGames[gameNumber].connectedSocket);
        tttGames[gameNumber].active = 0;
        return 0;
    }

    return 1;
}

/**
 * Sends a message to a client.
 * @param  move: The move to make.
 * @param  complete: The game complete code to send.
 * @param  completeDescriptor: The game complete descritor code to send.
 * @param  gameNumber: The game number to send.
 * @param  clientAddress: The address of the client to send to.
 * @retval None.
 */
void sendMessage(int command, int move, int complete, int completeDescriptor, int gameNumber, unsigned char clientSequenceNum, int connectedSocket, unsigned char messageStore[MESSAGE_SIZE])
{
    unsigned char moveByte = move;
    unsigned char completeByte = complete;
    unsigned char completeDescriptorByte = completeDescriptor;
    unsigned char commandByte = MOVE_COMMAND;
    unsigned char gameNumByte = gameNumber;
    unsigned char sequenceByte = clientSequenceNum;

    messageStore[0] = VERSION;
    messageStore[1] = moveByte;
    messageStore[2] = completeByte;
    messageStore[3] = completeDescriptorByte;
    messageStore[4] = commandByte;
    messageStore[5] = gameNumByte;
    messageStore[6] = sequenceByte;

    //Send message
    if (send(connectedSocket, messageStore, MESSAGE_SIZE, TCP_FLAGS) <= 0)
    {
        printf("--- ERROR - Client %d - Couldn't write to socket, Closing game. Error: ", gameNumber);
        perror("");
        close(tttGames[gameNumber].connectedSocket);
        tttGames[gameNumber].active = 0;
    }

    debugPacket(messageStore, SENT, ORIGINAL);
}

void sendPacket(unsigned char packet[MESSAGE_SIZE], int connectedSocket)
{
    //Send message
    if (send(connectedSocket, packet, MESSAGE_SIZE, TCP_FLAGS) <= 0)
    {
        perror("Error sending packet");
    }
}

/**
 * Initialize the state of a tictactoe game board.
 * @param  board[ROWS][COLUMNS]: The board matrix.
 * @retval None.
 */
void initSharedState(char board[ROWS][COLUMNS])
{
    /* this just initializing the shared state aka the board */
    int i, j, count = 1;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
        {
            board[i][j] = count + '0';
            count++;
        }
}

/**
 * Validate and place a move on the board.
 * @param  board[ROWS][COLUMNS]: The board to place move on.
 * @param  move: The move to place on board.
 * @param  player: The player placing the move (client or server).
 * @retval 1 if move placed successfully; 0 otherwise.
 */
int placeMove(char board[ROWS][COLUMNS], int move, int player)
{
    char mark = (player == 1) ? 'O' : 'X'; //depending on who the player is, either us x or o

    if (move < 1 || move > 9)
    {
        return 0;
    }

    int choice = move;
    int row = (int)((choice - 1) / ROWS);
    int column = (choice - 1) % COLUMNS;

    if (board[row][column] == (choice + '0'))
    {
        board[row][column] = mark;
        return 1;
    }
    return 0;
}

/**
 * Check if a player has won the game.  Assumes this is called between each move made.
 * @param  board[ROWS][COLUMNS]: The game board to check.
 * @param  player: The last player to place a move.
 * @retval {win code} if player has won; {draw code} if tie; -1 otherwise.
 */
int checkWin(char board[ROWS][COLUMNS], int player)
{
    int winner = -2;
    if (player == SERVER_PLAYER)
    {
        winner = SERVER_WIN;
    }
    else if (player == CLIENT_PLAYER)
    {
        winner = CLIENT_WIN;
    }

    /************************************************************************/
    /* brute force check to see if someone won, or if there is a draw       */
    /************************************************************************/
    if (board[0][0] == board[0][1] && board[0][1] == board[0][2]) // row matches
        return winner;

    else if (board[1][0] == board[1][1] && board[1][1] == board[1][2]) // row matches
        return winner;

    else if (board[2][0] == board[2][1] && board[2][1] == board[2][2]) // row matches
        return winner;

    else if (board[0][0] == board[1][0] && board[1][0] == board[2][0]) // column
        return winner;

    else if (board[0][1] == board[1][1] && board[1][1] == board[2][1]) // column
        return winner;

    else if (board[0][2] == board[1][2] && board[1][2] == board[2][2]) // column
        return winner;

    else if (board[0][0] == board[1][1] && board[1][1] == board[2][2]) // diagonal
        return winner;

    else if (board[2][0] == board[1][1] && board[1][1] == board[0][2]) // diagonal
        return winner;

    else if (board[0][0] != '1' && board[0][1] != '2' && board[0][2] != '3' &&
             board[1][0] != '4' && board[1][1] != '5' && board[1][2] != '6' &&
             board[2][0] != '7' && board[2][1] != '8' && board[2][2] != '9')

        return DRAW; // Return of DRAW means game over
    else
        return -1; // return of -1 means keep playing
}

/**
 * Places a valid move for the server.  
 * @param  board[ROWS][COLUMNS]: The board to place a move on.
 * @retval The move placed by server.
 */
int placeServerMove(char board[ROWS][COLUMNS])
{
    int choice = 0;
    do
    {
        choice++;
    } while (placeMove(board, choice, SERVER_PLAYER) != 1);
    return choice;
}

/**
 * Close all open sockets.  
 * @retval None.
 */
void closeSockets()
{
    //TODO: Send server shutdown
    close(globTCPSocket);
    close(globMulticastSocket);
    int i;
    for (i = 0; i < MAX_NUMBER_OF_ACTIVE_GAMES; i++)
    {
        close(tttGames[i].connectedSocket);
    }
}

/**
 * Attempt to start a new game, message client with game number if successful
 * Message with error if all games are full
 * @param clientAddress - the client address
 */
void allocateGame(int connectedSocket, struct sockaddr_in clientAddress)
{
    //Check if client already has open game
    int activeGame = findGameByAddress(clientAddress);

    //Client has open game, send retry and close
    if (activeGame != -1)
    {
        unsigned char messageStore[MESSAGE_SIZE];
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_RETRY, -1, 0, connectedSocket, messageStore);
        printf("--- RETRY - Client Rejected and Existing Game %d Canceled ---\n", activeGame);

        close(tttGames[activeGame].connectedSocket);
        tttGames[activeGame].active = 0;
        close(connectedSocket);
        return;
    }

    //Find the next open game
    int gameNumber = 0;
    int gameFound = 0;
    unsigned char i = 0;
    for (i = 0; i < MAX_NUMBER_OF_ACTIVE_GAMES; i++)
    {
        if (tttGames[i].active == 0)
        {
            gameFound = 1;
            gameNumber = i;
            break;
        }
    }

    //All game slots were full
    if (gameFound == 0)
    {
        unsigned char messageStore[MESSAGE_SIZE];
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_OUT_OF_RESOURCES, gameNumber, 0, connectedSocket, messageStore);
        printf("--- OUT OF RESOURCES - New Client Rejected\n");
        close(connectedSocket);
    }
    else
    {
        //initialize game and respond to client
        tttGames[gameNumber].active = 1;
        tttGames[gameNumber].address = clientAddress;
        tttGames[gameNumber].connectedSocket = connectedSocket;
        printf("--- NEW CLIENT CONNECTED - Client %d\n", gameNumber);
    }
}

void startGame(int gameNumber, unsigned char clientSequenceNum)
{
    //Init game space and sequence num
    initSharedState(tttGames[gameNumber].board);
    tttGames[gameNumber].sequenceNumber = clientSequenceNum;
    sendMessage(MOVE_COMMAND, 0, 0, 0, gameNumber, clientSequenceNum + 1, tttGames[gameNumber].connectedSocket, tttGames[gameNumber].lastMessage);
    printf("--- NEW GAME - Client %d\n", gameNumber);
}

/**
 * Find active game for client if one exist.  
 * @param  address: The client address.
 * @retval The index of the clients active game; -1 if no game exists.
 */
int findGameByAddress(struct sockaddr_in address)
{
    int i = -1;
    int compare = 1;
    while (i < MAX_NUMBER_OF_ACTIVE_GAMES && compare)
    {
        i++;
        if (tttGames[i].active == 1)
        {
            compare = memcmp(&address, &(tttGames[i].address), sizeof(address));
        }
    }
    if (!compare)
    {
        return i;
    }
    else
    {
        return -1;
    }
}

int findGameBySocket(int connectedSocket)
{
    int i = -1;
    int match = 0;
    while (i < MAX_NUMBER_OF_ACTIVE_GAMES && !match)
    {
        i++;
        if (tttGames[i].active == 1)
        {
            match = tttGames[i].connectedSocket == connectedSocket;
        }
    }
    if (match)
    {
        return i;
    }
    else
    {
        return -1;
    }
}

/**
 * Handle a move command by the client. Sends appropriate response and deactives game if necessary.
 * @param  activeGame: The index of a clients active game (or -1 if none).
 * @param  gameNumber: The gameNumber sent by client.
 * @param  move: The move sent by client.
 * @param  clientComplete: The complete code sent by client.
 * @param  clientCompleteDescriptor: The complete descriptor code sent by client.
 * @param  clientAddress: The client address.
 * @retval None.
 */
void handleMove(int activeGame, int command, int clientGameNum, int move, int clientComplete, int clientCompleteDescriptor, unsigned char clientSequenceNum)
{
    //Check that game number matches active game
    if (activeGame != clientGameNum)
    {
        printf("--- ERROR - Client %d - Malformed Request: Incorrect game number, Closing game\n", activeGame);
        //Send error
        unsigned char messageStore[MESSAGE_SIZE];
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, clientGameNum, clientSequenceNum + 1, tttGames[activeGame].connectedSocket, messageStore);
        close(tttGames[activeGame].connectedSocket);
        tttGames[activeGame].active = 0;
        return;
    }

    //Get client game
    struct tttGame *clientGame = &tttGames[activeGame];
    if (clientSequenceNum == (*clientGame).sequenceNumber)
    {
        //Bypass

        // //TODO:
        // //Should I resend or just wait for timeout to resend here
        // return;
    }
    else if (clientSequenceNum != (*clientGame).sequenceNumber + 2)
    {
        //Bypass

        // //Client sequence number is invalid
        // sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, clientGameNum, clientSequenceNum + 1, (*clientGame).connectedSocket, (*clientGame).lastMessage);
        // printf("--- ERROR - Client %d - Malformed Request: Invalid sequence number, Closing game\n", activeGame);
        // close(tttGames[activeGame].connectedSocket);
        // tttGames[activeGame].active = 0;
        // return;
    }
    else
    {
        (*clientGame).sequenceNumber = clientSequenceNum;
    }

    //Check for error message
    if (clientComplete == GAME_ERROR)
    {
        printf("--- ERROR - Client %d reported a general error, ending game\n");
        close((*clientGame).connectedSocket);
        (*clientGame).active = 0;
        return;
    }

    //Catch endgame response
    if (command == END_GAME_COMMAND)
    {
        printf("--- HANDSHAKE - Client %d - End Game Response\n", activeGame);
        close((*clientGame).connectedSocket);
        (*clientGame).active = 0;
        return;
    }

    //Validate and place client move
    if (placeMove((*clientGame).board, move, CLIENT_PLAYER) == 0)
    {
        //Invalid move -- Send error and end game
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, clientGameNum, clientSequenceNum + 1, (*clientGame).connectedSocket, (*clientGame).lastMessage);
        printf("--- ERROR - Client %d - Malformed Request: Invalid move, Closing game\n", activeGame);
        close((*clientGame).connectedSocket);
        (*clientGame).active = 0;
        return;
    }

    printf("--- MOVE - Client %d\n", activeGame);

    handleMoveAfterPlaced(clientGame, clientComplete, activeGame, clientCompleteDescriptor, clientSequenceNum, clientGameNum);
}

void handleMoveAfterPlaced(struct tttGame *clientGame, int clientComplete, int activeGame, int clientCompleteDescriptor, unsigned char clientSequenceNum, int clientGameNum)
{
    //Check for game complete and winner
    int win = checkWin((*clientGame).board, CLIENT_PLAYER);
    int complete = GAME_IN_PROGRESS;
    if (win != -1)
    {
        complete = GAME_COMPLETE;
    }

    //Check if client claims game complete
    if (clientComplete == GAME_COMPLETE)
    {
        //Handle endgame handshake
        handleEndgame(activeGame, win, complete, clientComplete, clientCompleteDescriptor, clientSequenceNum);
    }
    else if (complete == GAME_COMPLETE)
    {
        //Game is complete but client did not claim appropiately
        //Malformed request
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, clientGameNum, clientSequenceNum + 1, (*clientGame).connectedSocket, (*clientGame).lastMessage);
        printf("--- ERROR - Client %d - Malformed Request: Expected game complete, Clsoing game\n", activeGame);
        close((*clientGame).connectedSocket);
        (*clientGame).active = 0;
        return;
    }
    else
    {
        //Get and place valid server move
        int serverMove = placeServerMove((*clientGame).board);

        //Check game complete
        win = checkWin((*clientGame).board, SERVER_PLAYER);
        complete = GAME_IN_PROGRESS;
        if (win != -1)
        {
            complete = GAME_COMPLETE;
            printf("--- GAME OVER - Client %d\n", activeGame);
        }

        //Send move to client
        sendMessage(MOVE_COMMAND, serverMove, complete, win, clientGameNum, clientSequenceNum + 1, (*clientGame).connectedSocket, (*clientGame).lastMessage);
    }
}

/**
 * Initalizes the array of tttGames with MAX_NUMBER_OF_ACTIVE_GAMES inactive games. 
 * @retval None.
 */
void initGamesArray()
{
    tttGames = malloc(MAX_NUMBER_OF_ACTIVE_GAMES * sizeof(struct tttGame));
    int i;
    for (i = 0; i < MAX_NUMBER_OF_ACTIVE_GAMES; i++)
    {
        tttGames[i].active = 0;
    }
}

void handleEndgame(int gameNumber, int win, int complete, int clientComplete, int clientCompleteDescriptor, unsigned char clientSequenceNum)
{
    //Get client game
    struct tttGame *clientGame = &tttGames[gameNumber];

    //Check that game is actually over
    if (complete != GAME_COMPLETE)
    {
        //Game not complete or client didn't set game complete byte
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, gameNumber, clientSequenceNum + 1, (*clientGame).connectedSocket, (*clientGame).lastMessage);
        printf("--- ERROR - Client %d - Malformed Request: Invalid game complete recieved, Closing game\n", gameNumber);
        close((*clientGame).connectedSocket);
        (*clientGame).active = 0;
        return;
    }

    //Check that winners match
    if (win != clientCompleteDescriptor)
    {
        //Game winners do not match
        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, gameNumber, clientSequenceNum + 1, (*clientGame).connectedSocket, (*clientGame).lastMessage);
        printf("--- ERROR - Client %d - Malformed Request: Mismatch endgame results, Closing game\n", gameNumber);
        close((*clientGame).connectedSocket);
        (*clientGame).active = 0;
        return;
    }

    //Endgame response
    sendMessage(END_GAME_COMMAND, 0, complete, win, gameNumber, clientSequenceNum + 1, (*clientGame).connectedSocket, (*clientGame).lastMessage);
    printf("--- HANDSHAKE - Client %d - End Game Sent\n", gameNumber);
    close((*clientGame).connectedSocket);
    (*clientGame).active = 0;
}

void setupSocketSets(fd_set *readSet, fd_set *exceptSet)
{
    //Set the socket sets
    FD_ZERO(readSet);
    FD_ZERO(exceptSet);
    //Include listening port
    FD_SET(globTCPSocket, readSet);
    FD_SET(globTCPSocket, exceptSet);
    FD_SET(globMulticastSocket, readSet);
    FD_SET(globMulticastSocket, exceptSet);

    //Set socket for all active games
    int i;
    for (i = 0; i < MAX_NUMBER_OF_ACTIVE_GAMES; i++)
    {
        if (tttGames[i].active)
        {
            FD_SET(tttGames[i].connectedSocket, readSet);
            FD_SET(tttGames[i].connectedSocket, exceptSet);
        }
    }
}

void onSelect(fd_set *readSet, fd_set *exceptSet)
{
    //Handle exceptions
    if (exceptSet != NULL)
    {
        if (FD_ISSET(globTCPSocket, exceptSet))
        {
            perror("Error: Exception thrown on TCP Socket");
            closeSockets();
            exit(-1);
        }
        if (FD_ISSET(globMulticastSocket, exceptSet))
        {
            perror("Error: Exception thrown on Multicast Socket");
            closeSockets();
            exit(-1);
        }
        int i;
        for (i = 0; i < MAX_NUMBER_OF_ACTIVE_GAMES; i++)
        {
            if (tttGames[i].active && FD_ISSET(tttGames[i].connectedSocket, exceptSet))
            {
                printf("--- ERROR - Client %d - Socket exception thrown, Closing game\n", i);
                close(tttGames[i].connectedSocket);
                tttGames[i].active = 0;
            }
        }
    }

    //Handle reads
    if (readSet != NULL)
    {
        if (FD_ISSET(globTCPSocket, readSet))
        {
            acceptClient();
        }
        if (FD_ISSET(globMulticastSocket, readSet))
        {
            handleMulticast();
        }
        int i;
        for (i = 0; i < MAX_NUMBER_OF_ACTIVE_GAMES; i++)
        {
            if (tttGames[i].active && FD_ISSET(tttGames[i].connectedSocket, readSet))
            {
                //Recieve message from a client and process if valid
                //Blocks temporarily for read
                unsigned char messageBuffer[MAX_MESSSAGE_SIZE];
                if (recvMessage(messageBuffer, i))
                {
                    //Debug
                    debugPacket(messageBuffer, RECEIVED, ORIGINAL);

                    //Find active game
                    int activeGame = i;

                    //Parse message from client
                    int clientVersion = messageBuffer[0];
                    int clientMove = messageBuffer[1];
                    int clientComplete = messageBuffer[2];
                    //If version 4+ get clientCompleteInfo byte
                    int clientCompleteInfo = -1;
                    if (messageBuffer[0] >= 4)
                    {
                        clientCompleteInfo = messageBuffer[3];
                    }
                    //If version 5+ get command and game bytes
                    int clientCommand = -1;
                    int clientGameNum = -1;
                    if (messageBuffer[0] >= 5)
                    {
                        clientCommand = messageBuffer[4];
                        clientGameNum = messageBuffer[5];
                    }
                    //If version 6+ get sequence number byte
                    unsigned char clientSequenceNum = 0;
                    if (clientVersion >= 6)
                    {
                        clientSequenceNum = messageBuffer[6];
                    }

                    //Handle Command
                    if (clientCommand == NEW_GAME_COMMAND)
                    {
                        startGame(activeGame, clientSequenceNum);
                    }
                    else if (clientCommand == RECONNECT_COMMAND)
                    {
                        unsigned char boardBytes[9];
                        int i = 0;
                        for (i = 0; i < 9; i++)
                        {
                            boardBytes[i] = messageBuffer[i + 7];
                        }

                        reconnectGame(activeGame, boardBytes);
                    }
                    else if (clientCommand == MOVE_COMMAND || clientCommand == END_GAME_COMMAND)
                    {
                        handleMove(activeGame, clientCommand, clientGameNum, clientMove, clientComplete, clientCompleteInfo, clientSequenceNum);
                    }
                    else
                    {
                        printf("--- ERROR - Client %d - Malformed Request: Incorrect game number, Closing game\n", activeGame);
                        //Send error
                        unsigned char messageStore[MESSAGE_SIZE];
                        sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, clientGameNum, clientSequenceNum + 1, tttGames[activeGame].connectedSocket, messageStore);
                        close(tttGames[activeGame].connectedSocket);
                        tttGames[activeGame].active = 0;
                        return;
                    }
                }
            }
        }
    }
}

void acceptClient()
{
    //Declare client socket
    struct sockaddr_in clientAddress;
    socklen_t clientAddressLength;
    //Initialize clientAddressLength
    clientAddressLength = sizeof(clientAddress);

    int connectedSocket = accept(globTCPSocket, (struct sockaddr *)&clientAddress, &clientAddressLength);

    if (connectedSocket == 0)
    {
        perror("--- ERROR - Problem accepting client: ");
        exit(-1);
    }

    //Allocate resources and start game
    allocateGame(connectedSocket, clientAddress);
}

void debugPacket(unsigned char buf[MAX_MESSSAGE_SIZE], int sentOrReceived, int repeatOrNot)
{
    if (DEBUG_MODE)
    {
        if (sentOrReceived == SENT)
        {
            printf("\nSENT - ");
        }
        else
        {
            printf("\nRECEIVED - ");
        }
        if (repeatOrNot == ORIGINAL)
        {
            printf("ORIGINAL\n");
        }
        else
        {
            printf("REPEAT\n");
        }

        printf("[Byte 1] Version = %d\n", buf[0]);
        printf("[Byte 2] Position = %d\n", buf[1]);

        printf("[Byte 3] Game State = %d ", buf[2]);
        switch (buf[2])
        {
        case 0:
            printf("(Game in Progress)\n");
            switch (buf[3])
            {
            case 0:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Game in progress)\n");
                break;
            default:
                printf("Unknown\n");
            }
            break;
        case 1:
            printf("(Game Complete)\n");
            switch (buf[3])
            {
            case 1:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Draw)\n");
                break;
            case 2:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Client Win)\n");
                break;
            case 3:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Server Win)\n");
                break;
            default:
                printf("Unknown\n");
            }
            break;
        case 2:
            printf("(Game Error)\n");
            switch (buf[3])
            {
            case 1:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Out of resources)\n");
                break;
            case 2:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Malformed Request)\n");
                break;
            case 3:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Server Shutdown)\n");
                break;
            case 4:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Client game timeout)\n");
                break;
            case 5:
                printf("[Byte 4] Modifier = %d ", buf[3]);
                printf("(Try again)\n");
                break;
            default:
                printf("Unknown\n");
            }
            break;
        default:
            printf("Unknown Byte\n");
        }

        printf("[Byte 5] Command = %d ", buf[4]);
        switch (buf[4])
        {
        case 0:
            printf("(New Game)\n");
            break;
        case 1:
            printf("(Move)\n");
            break;
        case 2:
            printf("(End Game)\n");
            break;
        default:
            printf("Unknown\n");
        }
        printf("[Byte 6] Game Num = %d\n", buf[5]);
        printf("[Byte 7] Sequence = %d\n", buf[6]);
    }
}

void handleMulticast()
{
    unsigned char messageBuffer[MESSAGE_SIZE];

    //Declare client socket
    struct sockaddr_in clientAddress;
    socklen_t clientAddressLength;
    //Initialize clientAddressLength
    clientAddressLength = sizeof(clientAddress);

    errno = 0;
    int bytesRead = recvfrom(globMulticastSocket, &messageBuffer, MESSAGE_SIZE, DATAGRAM_FLAGS, (struct sockaddr *)&clientAddress, &clientAddressLength);

    if (bytesRead < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            //Blocking read timeout
            return;
        }
        perror("Error: Problem reading from socket");
        closeSockets();
        exit(-1);
    }
    // if (bytesRead < MIN_MESSAGE_SIZE)
    // {
    //     printf("--- ERROR - Multicast message too short\n");
    //     return;
    // }

    if (!(messageBuffer[0] >= EARLIEST_VERSION))
    {
        printf("--- ERROR - Multicast received using incompatible protocol version\n");
        return;
    }

    if (!(messageBuffer[1]) == 1)
    {
        printf("--- ERROR - Multicast received with invalid command\n");
        return;
    }

    printf("--- MULTICAST - Multicast request received\n");

    unsigned char responseBuffer[MESSAGE_SIZE];
    unsigned char versionByte = VERSION;
    unsigned char commandByte = 2;
    unsigned int portHTON = htons(globServerPort);
    //TODO: Iterate here as well possibly
    // *portBytes = ;

    responseBuffer[0] = versionByte;
    responseBuffer[1] = commandByte;
    memcpy(&(responseBuffer[2]), &portHTON, 2);

    printf("Port: %ld\n", globServerPort);
    printf("Response byte 1: %x\n", responseBuffer[0]);
    printf("Response byte 2: %x\n", responseBuffer[1]);
    printf("Response byte 3: %x\n", responseBuffer[2]);
    printf("Response byte 4: %x\n", responseBuffer[3]);

    // printf("Response byte 3: %x\n", portBytes[0]);
    // printf("Response byte 4: %x\n", portBytes[1]);

    //Send datagram
    if (sendto(globMulticastSocket, responseBuffer, MESSAGE_SIZE, DATAGRAM_FLAGS, (struct sockaddr *)&clientAddress, sizeof(clientAddress)) <= 0)
    {
        perror("Error: Problem sending multicast response");
    }

    printf("--- MULTICAST - Multicast respnse sent\n");

    return;
}

void reconnectGame(int activeGame, unsigned char boardBytes[9])
{
    //Get tttGame
    struct tttGame *clientGame = &tttGames[activeGame];

    //Init game space and sequence num
    initSharedState((*clientGame).board);
    (*clientGame).sequenceNumber = 0;
    int i;
    for (i = 0; i < 9; i++)
    {
        if (boardBytes[i] == 1)
        {
            placeMove((*clientGame).board, i + 1, CLIENT_PLAYER);
        }
        else if (boardBytes[i] == 2)
        {
            placeMove((*clientGame).board, i + 1, SERVER_PLAYER);
        }
        else if (boardBytes[i] != 0)
        {
            //Invalid board state
            sendMessage(MOVE_COMMAND, 0, GAME_ERROR, ERROR_MALFORMED_REQUEST, activeGame, (*clientGame).sequenceNumber + 2, (*clientGame).connectedSocket, (*clientGame).lastMessage);
            printf("--- ERROR - Client %d - Malformed Request: Invalid reconnect board state, Closing game\n", activeGame);
            close((*clientGame).connectedSocket);
            (*clientGame).active = 0;
            return;
        }
    }
    printf("--- NEW GAME FROM RECONNECT - Client %d\n", activeGame);

    if (DEBUG_MODE)
    {
        print_board((*clientGame).board);
    }

    //Make move
    handleMoveAfterPlaced(clientGame, GAME_IN_PROGRESS, activeGame, 0, tttGames[activeGame].sequenceNumber + 2, activeGame);

    //Pretty sure there will be errors thown here if they try to recconnect with a final move
}

//For debug
/**
 * Visually print the ASCII board to the screen
 * */
void print_board(char board[ROWS][COLUMNS])
{
    /*****************************************************************/
    /* brute force print out the board and all the squares/values    */
    /*****************************************************************/

    printf("\n\n\n\tCurrent TicTacToe Game\n\n");

    printf("Player 1 (O)  -  Player 2 (X)\n\n\n");

    printf("     |     |     \n");
    printf("  %c  |  %c  |  %c \n", board[0][0], board[0][1], board[0][2]);

    printf("_____|_____|_____\n");
    printf("     |     |     \n");

    printf("  %c  |  %c  |  %c \n", board[1][0], board[1][1], board[1][2]);

    printf("_____|_____|_____\n");
    printf("     |     |     \n");

    printf("  %c  |  %c  |  %c \n", board[2][0], board[2][1], board[2][2]);

    printf("     |     |     \n\n");
}
