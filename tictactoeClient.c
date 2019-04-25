#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>


/* #define section*/

//Project Lab Defines
#define MC_PORT 1818
#define MC_GROUP "239.0.0.7"
#define MAX_IP_LENGTH 18
#define MAX_PORT_LENGTH 5
#define NOT_CONNECTED -1

//Game version and general info
#define ROWS 3
#define COLUMNS 3
#define MAX_BUFFER_SIZE 1000
#define MAX_BUFFER_SIZE_MULTICAST 1000
#define VERSION 8
#define LAST_SUPPORTED_VERSION 8
#define TIMEOUT 10
#define MAX_RETRIES 3
#define RETRY_SLEEP_TIME 3
#define NUMBER_OF_SPACES 9

//Protocol Byte 5 Defines
#define NEW_GAME 0
#define MOVE 1
#define END_GAME 2
#define UNUSED_BYTE 0

//Protocol Bytes 4 (Error) Defines
#define SERVER_ERROR 2
#define OUT_OF_RESOURCES 1
#define MALFORMED_REQUEST 2
#define SERVER_SHUTDOWN 3
#define CLIENT_TIMEOUT 4
#define TRY_AGAIN 5
#define CLIENT_WINS 2
#define SERVER_WINS 3
#define DRAW 1
#define RECONNECT 3

//State Information Defines
#define GAME_COMPLETE 1
#define IN_PROGRESS 0
#define WIN_OR_LOSE 1
#define GAME_OVER 1
#define TIE 2

//Board state defines
#define SPACE_EMPTY 0
#define SPACE_CLIENT 1
#define SPACE_SERVER 2

//Debug defines
#define SENT 1
#define RECEIVED 2
#define ORIGINAL 1
#define REPEAT 2
#define DEBUG_MODE 1 //Switch to 0 to disable packet output

unsigned char sequenceNumber = 1;
unsigned char gameNumber;              //Number assigned to this client from the server
unsigned char clientBuffer[MAX_BUFFER_SIZE]; //Storage for network data
unsigned char serverBuffer[MAX_BUFFER_SIZE];
unsigned char storedBuffer[MAX_BUFFER_SIZE]; //Storage for last message sent
char board[ROWS][COLUMNS];             //Board as a 3x3 array of chars
int socket_descriptor;                 //Socket connection to server
int multicast_descriptor;
struct sockaddr_in server_address;     //Socket connection to server
struct sockaddr_in multicast_address;
socklen_t fromLength;                  //Length of server message
socklen_t multicastLength;                  //Length of server message
int messageRetries = 1;
int reconnected = 0;
int serversRead = 0;

long convertPort(char *strPort);
int isValidIpAddress(char *ipAddress);
int checkwin(char board[ROWS][COLUMNS]);        //Return correct winState
int tictactoe();                                //Run the game
int initSharedState(char board[ROWS][COLUMNS]); //Initialize board
int getPlayerChoice();                          //Retrieve choice from command line
unsigned char getClientWinStatus(int winState);
int updateBoard(int row, int col, int choice, char mark, int client); //Put the mark at the given row/col
unsigned char checkEndGame(int winStatus, int playerWin);           //Check if its time to exit the program
void checkConnection(int val);                             //Confirm server still connected
void checkRead(int val);                                   //Confirm server still connected
void print_board(char board[ROWS][COLUMNS]);
void prepareSocket(char *argv[]);
void prepareMove(int *row, int *column, char mark);
void sendClientMove(int choice, unsigned char clientWinStatus, unsigned char winState);
void parseServerData(unsigned char *serverVersion, unsigned char *serverChoice, unsigned char *serverWin, unsigned char *serverModifier, int clientWinStatus);
void initiateNewGame();
void retryConnection();
void incrementSequenceNumber();
void debugPacket(unsigned char buf[MAX_BUFFER_SIZE], int sentOrReceived, int repeatOrNot);
void storeLastMessageSent();
void repeatMessage();
void sendAck(unsigned char win);
void getNetworkBoard(unsigned char* convertedBoard);
void sendReconnect();
int reconnectSocket(char serverIP[MAX_IP_LENGTH], short portNumber);
void messageMulticast();


int main(int argc, char *argv[])
{

  if (argc != 3)
  {
    perror("Usage is ./tttClient <server_port> <server_ip-address> \n");
    exit(EXIT_FAILURE);
  }

  if (!isValidIpAddress(argv[2]))
  {
    perror("Invalid IP Address");
    return (EXIT_FAILURE);
  }

  initSharedState(board); // Initialize the 'game' board
  tictactoe(board, argv); // call the 'game'
  return 0;
}

/**
 * Contains the core game loop
 * */
int tictactoe(char board[ROWS][COLUMNS], char *argv[])
{
  unsigned int choice; // used for keeping track of choice user makes
  int winState;
  int row, column;
  int bytes_received;
  char mark; // either an 'x' or an 'o'
  unsigned char clientWinStatus;
  unsigned char serverVersion, serverChoice, serverWin, serverModifier;

  prepareSocket(argv);

  //Initiate new game
  initiateNewGame();

  /* loop, first print the board, then ask player to make a move */
  print_board(board); // call function to print the board on the screen

  do
  {
    choice = getPlayerChoice(); //Get the player choice, as an integer

    mark = 'X'; //We're the client, we always go first and are X
    row = (int)((choice - 1) / ROWS);
    column = (choice - 1) % COLUMNS;

    choice = updateBoard(row, column, choice, mark, 1);
    print_board(board); //Print after the player makes their choice

    //Check win, can't end the game yet, need to notify server even if we have won
    winState = checkwin(board);

    //Convert Tie/Win/Loss/Ongoing into protocol compatible digit
    clientWinStatus = getClientWinStatus(winState);

    //Construct data to send & send it
    sendClientMove(choice, clientWinStatus, winState);

    //After we send our move, we can actually end the game if we've won
    clientWinStatus = checkEndGame(winState, 1);

    //Commence server turn/response
    bytes_received = read(socket_descriptor, &serverBuffer, MAX_BUFFER_SIZE);

    //TODO:reconnect to server in this function, then ideally return as normal here and continue
    checkRead(bytes_received);

    incrementSequenceNumber();
    parseServerData(&serverVersion, &serverChoice, &serverWin, &serverModifier, clientWinStatus);

    mark = 'O';
    row = (int)((serverChoice - 1) / ROWS);
    column = (serverChoice - 1) % COLUMNS;
    updateBoard(row, column, serverChoice, mark, 0);
    print_board(board);

  } while (1);

  return 0;
}

/**
 * Send a NEW_GAME Request to server and initialize
 * */
void initiateNewGame()
{
  int bytes_sent;
  int bytes_received;
  unsigned char *buf = clientBuffer;

  //Clear the buffer & fill it with new game data
  bzero(buf, MAX_BUFFER_SIZE);
  buf[0] = VERSION;
  buf[5] = NEW_GAME;
  buf[6] = sequenceNumber;

  //Send NEW_GAME message to server
  debugPacket(clientBuffer, SENT, ORIGINAL);
  bytes_sent = write(socket_descriptor, clientBuffer, MAX_BUFFER_SIZE);
  storeLastMessageSent();
  checkConnection(bytes_sent);

  //Receive response, with game # value
  bytes_received = read(socket_descriptor, &serverBuffer, MAX_BUFFER_SIZE);
  checkRead(bytes_received);
  incrementSequenceNumber();

  //If we're here, connection was successful, retrieve game number
  gameNumber = serverBuffer[5];
}

/**
 * Send the client move with appropiate win status
 * */
void sendClientMove(int choice, unsigned char clientWinStatus, unsigned char winState)
{
  unsigned char *buf = clientBuffer;
  int bytes_sent;

  incrementSequenceNumber(); //Increment the sequence number for this message

  buf[0] = VERSION;
  buf[1] = choice;
  buf[2] = clientWinStatus;

  if(winState == TIE){
    buf[3] = DRAW;
  }else if (winState != IN_PROGRESS){
    buf[3] = CLIENT_WINS; //this method only called if we have won or tied, never if server sent us a winning move. (So we never send "SERVER_WINS" here)
  }else{
    buf[3] = IN_PROGRESS;
  }

  buf[4] = MOVE;

  buf[5] = gameNumber;
  buf[6] = sequenceNumber;
  debugPacket(clientBuffer, SENT, ORIGINAL);
  bytes_sent = write(socket_descriptor, clientBuffer, MAX_BUFFER_SIZE);
  storeLastMessageSent();
  checkConnection(bytes_sent);
}

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

/**
 * Intiialize the board to a fresh state
 * */
int initSharedState(char board[ROWS][COLUMNS])
{
  /* this just initializing the shared state aka the board */
  int i, j, count = 1;
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++)
    {
      board[i][j] = count + '0';
      count++;
    }
  return 0;
}

/**
 * Check the board to determine if a player has won, or if there is a draw
 * */
int checkwin(char board[ROWS][COLUMNS])
{
  /************************************************************************/
  /* brute force check to see if someone won, or if there is a draw       */
  /* return a 0 if the game is 'over' and return -1 if game should go on  */
  /************************************************************************/
  if (board[0][0] == board[0][1] && board[0][1] == board[0][2]) // row matches
    return WIN_OR_LOSE;

  else if (board[1][0] == board[1][1] && board[1][1] == board[1][2]) // row matches
    return WIN_OR_LOSE;

  else if (board[2][0] == board[2][1] && board[2][1] == board[2][2]) // row matches
    return WIN_OR_LOSE;

  else if (board[0][0] == board[1][0] && board[1][0] == board[2][0]) // column
    return WIN_OR_LOSE;

  else if (board[0][1] == board[1][1] && board[1][1] == board[2][1]) // column
    return WIN_OR_LOSE;

  else if (board[0][2] == board[1][2] && board[1][2] == board[2][2]) // column
    return WIN_OR_LOSE;

  else if (board[0][0] == board[1][1] && board[1][1] == board[2][2]) // diagonal
    return WIN_OR_LOSE;

  else if (board[2][0] == board[1][1] && board[1][1] == board[0][2]) // diagonal
    return WIN_OR_LOSE;

  else if (board[0][0] != '1' && board[0][1] != '2' && board[0][2] != '3' &&
           board[1][0] != '4' && board[1][1] != '5' && board[1][2] != '6' &&
           board[2][0] != '7' && board[2][1] != '8' && board[2][2] != '9')

    return TIE; // Return of 2 means game over - tie
  else
    return IN_PROGRESS; // return of 0 means keep playing
}

/**
 * Convert port from cmdline to appropriate format
 * */
void prepareSocket(char *argv[])
{
  //Begin server connection
  long portNumber;
  char serverIP[29];

  long lPort = convertPort(argv[1]);
  portNumber = lPort;

  //Initialize socket
  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  portNumber = strtol(argv[1], NULL, 10);
  if (portNumber == 0)
  {
    perror("Error: Problem converting port number");
    exit(-1);
  }
  strcpy(serverIP, argv[2]);

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(portNumber);
  server_address.sin_addr.s_addr = inet_addr(serverIP);

  //Setup timeout
  //https://stackoverflow.com/questions/13547721/udp-socket-set-timeout
  // struct timeval tv;
  // tv.tv_sec = TIMEOUT;
  // tv.tv_usec = 0;
  // if (setsockopt(socket_descriptor, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
  // {
  //   perror("Error");
  // }

	//Attempt to connect
	if(connect(socket_descriptor, (struct sockaddr *)&server_address, sizeof(struct sockaddr_in))<0)
	{
		close(socket_descriptor);
		perror("Error connecting to stream socket");
		exit(EXIT_FAILURE);
	}

  //Initialize globClientAddressLength
  fromLength = sizeof(server_address);
}

/**
 * Check whether there was an error when sending data to the server
 * */
void checkConnection(int val)
{
  if (val <= 0)
  {
    perror("The server did not receive the data - Connection failure\n");
    exit(1);
  }
}

/**
 * Attempt to initiate a new game again.
 * If it fails, retry up to MAX_RETRIES amount of times automatically
 * */
void retryInit(){
  int bytes_sent;
  int bytes_received;
  unsigned char *buf = clientBuffer;
  int connectionFound = 0;

  while((messageRetries < MAX_RETRIES) && (connectionFound == 0)){
    printf("Retrying New Game Request (%d/%d) in %d seconds\n", messageRetries, MAX_RETRIES, RETRY_SLEEP_TIME);
    sleep(RETRY_SLEEP_TIME);

    //Clear the buffer & fill it with new game data
    bzero(buf, MAX_BUFFER_SIZE);
    buf[0] = VERSION;
    buf[5] = NEW_GAME;
    buf[6] = sequenceNumber;

    //Send NEW_GAME message to server
    debugPacket(clientBuffer, SENT, ORIGINAL);
    bytes_sent = write(socket_descriptor, clientBuffer, MAX_BUFFER_SIZE);
    storeLastMessageSent();
    checkConnection(bytes_sent);

    //Receive response, with game # value
    bytes_received = read(socket_descriptor, &serverBuffer, MAX_BUFFER_SIZE);

    if(bytes_received <= 0){
      printf("Received no server response (retry %d/%d)\n",messageRetries,MAX_RETRIES);
      messageRetries++;
      repeatMessage();
    }

    if ((serverBuffer[2] == SERVER_ERROR) && (serverBuffer[3] == OUT_OF_RESOURCES)){
      printf("OUT_OF_RESOURCES - The server can't accept a new game at this time\n");
      messageRetries++;
    }else if (serverBuffer[2] == SERVER_ERROR){
      printf("Unknown Error When Retrying Connection - Exiting\n");
      exit(EXIT_FAILURE);
    }else{
      connectionFound = 1;
    }
  }

  if(messageRetries == MAX_RETRIES + 1){
    printf("Retry limit reached, try again later.\n");
    exit(EXIT_FAILURE);
  }

  //If we're here, connection was successful, retrieve game number
  gameNumber = serverBuffer[5];
  messageRetries = 1;
}

/**
 * Check the status of the recvfrom command for 3 things:
 * 1: If it timed out - Send previous message again if so (up to MAX_RETRIES times)
 * 2: If the server reported an error - Handle it appropaitely if possible, otherwise close
 * 3: If server reports duplicate sequence number - Send appropriate message again
 * */
void checkRead(int val)
{
  if (val <= 0)
  {
    printf("Connection to server has failed - Searching for new connection\n");
    messageMulticast();  
  }else{
    debugPacket(serverBuffer, RECEIVED, ORIGINAL);
  }

  if (serverBuffer[2] == SERVER_ERROR)
  {
    switch (serverBuffer[3])
    {
    case OUT_OF_RESOURCES:
      printf("OUT_OF_RESOURCES - The server can't accept a new game at this time\n");
      retryInit();
      break;
    case MALFORMED_REQUEST:
      printf("MALFORMED_REQUEST - Server could not understand request\n");
      exit(EXIT_FAILURE);
      break;
    case SERVER_SHUTDOWN:
      printf("SERVER_SHUTDOWN - Connection to server has ended\n");
      exit(EXIT_FAILURE);
      break;
    case CLIENT_TIMEOUT:
      printf("CLIENT_TIMEOUT - Connection to server has timed out\n");
      exit(EXIT_FAILURE);
      break;
    case TRY_AGAIN:
      printf("TRY_AGAIN - Retrying new game request\n");
      retryInit();
      exit(EXIT_FAILURE);
      break;
    default:
      printf("UNKNOWN ERROR - Closing\n");
      exit(EXIT_FAILURE);
      break;
    }
  }

  //If we're here, we've successfully received a response from the server - so reset retry count
  messageRetries = 1;
}

/**
 * Retrieve player choice from the commandline
 * */
int getPlayerChoice()
{
  int tempChoice = 0;
  printf("Player 2, enter a number:  "); // print out player so you can pass game
  scanf("%d", &tempChoice);              //using scanf to get the choice

  while ((tempChoice < 1) || (tempChoice > 9))
  {
    printf("Valid choices are 1-9\n");
    printf("Player 2, enter a number:  ");
    scanf("%d", &tempChoice);
    getchar();
  }

  return tempChoice;
}

/**
 * Place the give move into the board array
 * Loop input again until move input is valid
 * */
int updateBoard(int row, int col, int choice, char mark, int client)
{
  if (board[row][col] == (choice + '0')){
    board[row][col] = mark;
  }else if (client == 1){
    while (!(board[row][col] == (choice + '0'))){
      choice = getPlayerChoice();
      mark = 'X';
      row = (int)((choice - 1) / ROWS);
      col = (choice - 1) % COLUMNS;
    }
    board[row][col] = mark;
  }else
  {
    //Player chose invalid move, close the connection and quit
    perror("Received Invalid move from the server, closing game");
    close(socket_descriptor);
    exit(1);
  }

  return choice;
}
/**
 * Determine 2 things:
 * 1: If the game is over or continuing
 * 2: If over, determine who won
 * */
unsigned char checkEndGame(int winState, int playerWin)
{
  unsigned char whoWins = IN_PROGRESS;

  if (winState == WIN_OR_LOSE)
  {
    if (playerWin == 1)
    {
      whoWins = CLIENT_WINS;
    }
    else
    {
      whoWins = SERVER_WINS;
    }
  }
  else if (winState == TIE)
  {
    whoWins = DRAW;
  }

  return whoWins;
}
/**
 * Retrieve the current win state as a protocol compatible digit
 * See byte 3 of protocol for example
 * */
unsigned char getClientWinStatus(int winState)
{
  unsigned char win;
  if (winState != IN_PROGRESS)
  {
    win = GAME_OVER;
  }
  else
  {
    win = IN_PROGRESS;
  }
  return win;
}

/**
 * Parse data sent from the server and do either:
 * 1: Receive their Handshake response if it exists
 * 2: Send a handshake response if appropriate
 * Also updates appropriate variables (version, server choice, server win status)
 * */ 
void parseServerData(unsigned char *serverVersion, unsigned char *serverChoice, unsigned char *serverWin, unsigned char *serverModifier, int clientWinStatus)
{
  *serverVersion = serverBuffer[0];
  *serverChoice = serverBuffer[1];
  *serverWin = serverBuffer[2];
  unsigned char serverStatus = serverBuffer[3];
  unsigned char serverCommand = serverBuffer[4];
  unsigned char serverGameNum = serverBuffer[5];
  unsigned char serverSequenceNum = serverBuffer[6];

  if (*serverVersion < LAST_SUPPORTED_VERSION)
  {
    perror("Version mismatch - Exiting\n");
    exit(1);
  }

  if (*serverVersion >= LAST_SUPPORTED_VERSION)
  {
    if(*serverWin == GAME_COMPLETE){
      if(clientWinStatus == IN_PROGRESS){
        //If they claim to have won/tie & we're still "in_progress", place their move and compare win states
        char mark = 'O';
        int row = ((int)((*serverChoice) - 1) / ROWS);
        int column = (*serverChoice - 1) % COLUMNS;
        updateBoard(row, column, *serverChoice, mark, 0);
        int winState = checkwin(board);
        int serverWin = checkEndGame(winState, 0);
        print_board(board);

        //Make sure their claim matches our result
        if(!serverWin == serverStatus){
          printf("ERROR: Win State Mismatch, exiting\n");
          exit(EXIT_FAILURE);
        }else{
          //if their win state is correct, we send the acknowledgement 
          sendAck(serverWin);
          if(serverStatus == SERVER_WINS){
            printf("You lose\n");
          }else if(serverStatus == CLIENT_WINS){
            printf("You Win\n");
          }else{
            printf("Draw\n");
          }
          exit(EXIT_SUCCESS);          
        }
      }else{
        //If our status is not "in_progress" then what we just received is the reply to our handshake
        if(serverStatus == SERVER_WINS){
          printf("You lose\n");
        }else if(serverStatus == CLIENT_WINS){
          printf("You Win\n");
        }else{
          printf("Draw\n");
        }
        exit(EXIT_SUCCESS);
      }
    }
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

//https://stackoverflow.com/questions/791982/determine-if-a-string-is-a-valid-ipv4-address-in-c
int isValidIpAddress(char *ipAddress)
{
  struct sockaddr_in sa;
  int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
  return result != 0;
}

/**
 * Increment the sequence number, rollover is necessary
 * */
void incrementSequenceNumber(){
  if(sequenceNumber == 255){
    sequenceNumber = 0;
  }else{
    sequenceNumber++;
  }
}

/**
 * Pretty print the content of the given buffer
 * Assuming it is formatted according to the protocol
 * 
 * Requires "DEBUG_MODE" to be defined as a non-zero value
 * */
void debugPacket(unsigned char buf[MAX_BUFFER_SIZE], int sentOrReceived, int repeatOrNot){

  if(DEBUG_MODE){ 
    if(sentOrReceived == SENT){
      printf("\nSENT - ");
    }else{
      printf("\nRECEIVED - ");
    }
    if(repeatOrNot == ORIGINAL){
      printf("ORIGINAL\n");
    }else{
      printf("REPEAT\n");
    }

    printf("[Byte 1] Version = %d\n",buf[0]);
    printf("[Byte 2] Position = %d\n",buf[1]);

    printf("[Byte 3] Game State = %d ",buf[2]);
    switch(buf[2]){
      case 0:
        printf("(Game in Progress)\n");
        switch(buf[3]){
            case 0:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Game in progress)\n");
              break;
            default:
              printf("Unknown\n");
        }
        break;
      case 1:
        printf("(Game Complete)\n");
        switch(buf[3]){
            case 1:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Draw)\n");
              break;
            case 2:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Client Win)\n");
              break;
            case 3:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Server Win)\n");
              break;
            default:
              printf("Unknown\n");
        }
        break;
      case 2:
        printf("(Game Error)\n");
        switch(buf[3]){
            case 1:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Out of resources)\n");
              break;
            case 2:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Malformed Request)\n");
              break;
            case 3:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Server Shutdown)\n");
              break;
            case 4:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Client game timeout)\n");
              break;
            case 5:
              printf("[Byte 4] Modifier = %d ",buf[3]);
              printf("(Try again)\n");
              break;
            default:
              printf("Unknown\n");
        }
        break;
      default:
        printf("Unknown Byte\n");
    }

    printf("[Byte 5] Command = %d ",buf[4]);
    switch(buf[4]){
      case 0:
        printf("(New Game)\n");
        break;
      case 1:
        printf("(Move)\n");
        break;
      case 2:
        printf("(End Game)\n");
        break;
      case 3:
        printf("(Reconnect)\n");
        break;
      default:
        printf("Unknown\n");
    }
    printf("[Byte 6] Game Num = %d\n",buf[5]);
    printf("[Byte 7] Sequence = %d\n",buf[6]);
  }
}

/**
 * Store the message we most previously sent
 * For use when a retry is necessary
 * */
void storeLastMessageSent(){
  memcpy(storedBuffer,clientBuffer,MAX_BUFFER_SIZE);
}

/**
 * Send the most previously sent message.
 * Receive the response afterward
 * Reapeat those 2 steps of another error occurs
 * */
void repeatMessage(){
    int bytes_sent, bytes_received = 0;
    //repeat previous message
    debugPacket(storedBuffer, SENT, REPEAT);
    bytes_sent = write(socket_descriptor, clientBuffer, MAX_BUFFER_SIZE);
    checkConnection(bytes_sent);

    //Receive response
    bytes_received = read(socket_descriptor, &serverBuffer, MAX_BUFFER_SIZE);
    checkRead(bytes_received);
}

/**
 * Send a correctly formatted handshake packet
 * For use when the server wins, and we need to acknowledge their win
 * */
void sendAck(unsigned char win){
  int bytes_sent, bytes_received = 0;

  incrementSequenceNumber();
  clientBuffer[0] = VERSION;
  clientBuffer[1] = 0;
  clientBuffer[2] = GAME_COMPLETE;
  clientBuffer[3] = win;
  clientBuffer[4] = END_GAME;
  clientBuffer[5] = gameNumber;
  clientBuffer[6] = sequenceNumber;
  debugPacket(clientBuffer, SENT, ORIGINAL);
  bytes_sent = write(socket_descriptor, clientBuffer, MAX_BUFFER_SIZE);
  storeLastMessageSent();
  checkConnection(bytes_sent);

  bytes_received = read(socket_descriptor, &serverBuffer, MAX_BUFFER_SIZE);
  if (bytes_received <= 0)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
          printf("Game complete\n");
      }
  }else{
    debugPacket(serverBuffer, RECEIVED, ORIGINAL);
    repeatMessage();
  }
}

//Pass in an unsigned char pointer with 9 spaces,
//it is updated with the current state of the board
//properly formatted for a reconnect
void getNetworkBoard(unsigned char* convertedBoard){

  unsigned char* tempConvBoard = convertedBoard;
  int i = 0;
  int j = 0;
  int k = 0;

  for(i = 0; i < ROWS; i++){
    for(j = 0; j < COLUMNS; j++){
      if(board[i][j] == 'X'){
        tempConvBoard[k] = SPACE_CLIENT;
      }else if(board[i][j] == 'O'){
        tempConvBoard[k] = SPACE_SERVER;
      }else{
        tempConvBoard[k] = SPACE_EMPTY;
      }
      k++;
    }
  }
}

void sendReconnect(){
  unsigned char* boardNetwork = malloc(NUMBER_OF_SPACES * sizeof(unsigned char)); 
  getNetworkBoard(boardNetwork);

  unsigned char *buf = clientBuffer;
  int bytes_sent, bytes_received;

  buf[0] = VERSION;
  buf[1] = UNUSED_BYTE;
  buf[2] = UNUSED_BYTE;
  buf[3] = UNUSED_BYTE;
  buf[4] = RECONNECT;

  buf[5] = UNUSED_BYTE; //this is now invalid since we're connecting to a new server
  buf[6] = UNUSED_BYTE; //No longer needed on TCP
  
  int i = 0;
  for(i = 0; i < 9; i++){
    buf[i+7] = boardNetwork[i];
  }

  debugPacket(clientBuffer, SENT, ORIGINAL);
  bytes_sent = write(socket_descriptor, clientBuffer, MAX_BUFFER_SIZE);

  storeLastMessageSent();
  checkConnection(bytes_sent);

  //Retrieve server response here
  //this updates the global buffer then we continue on as normal
  //but we need to store the new game number
  bytes_received = read(socket_descriptor, &serverBuffer, MAX_BUFFER_SIZE);
  debugPacket(serverBuffer, RECEIVED, ORIGINAL);
  checkRead(bytes_received);
  gameNumber = serverBuffer[5];
}

//Reconnect to given TCP socket
//Return 0 if successful, -1 if failed to connect;
int reconnectSocket(char serverIP[MAX_IP_LENGTH], short portNumber){

  int rc = 0;

  //Initialize socket
  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(portNumber);
  server_address.sin_addr.s_addr = inet_addr(serverIP);

	//Attempt to connect
	if(connect(socket_descriptor, (struct sockaddr *)&server_address, sizeof(struct sockaddr_in))<0)
	{
		close(socket_descriptor);
		perror("Error connecting to this socket\n");
    rc = -1;
	}


  //Initialize globClientAddressLength
  fromLength = sizeof(server_address);

  return rc;
}

void retrieveServerFromFile(){
  FILE* fp;
  char *ipBuffer = malloc(MAX_IP_LENGTH*sizeof(char));
  char porBuf[MAX_PORT_LENGTH];
  short port;

  int connected = NOT_CONNECTED;

  while(connected == NOT_CONNECTED){
  fp = fopen("servers.txt", "r");

  int i = 0;
  for(i = 0; i <= serversRead; i++){
    if(fscanf(fp, "%s %s", ipBuffer, porBuf) == EOF){
      printf("No servers left in file, closing...\n");
      exit(EXIT_FAILURE);
    };
  }
  
  serversRead++;

  fclose(fp);
  port = atoi(porBuf);

  connected = reconnectSocket(ipBuffer,port);
  }

  sendReconnect();
}

void messageMulticast(){
  unsigned char multicast_message[MAX_BUFFER_SIZE_MULTICAST];
  unsigned char multicast_response[MAX_BUFFER_SIZE_MULTICAST];
  short receivedPort;
  int rc;

  multicast_message[0] = VERSION;
  multicast_message[1] = 1;

  //Send message on the multicast
  multicast_descriptor = socket(AF_INET, SOCK_DGRAM, 0);

  multicast_address.sin_family = AF_INET;
  multicast_address.sin_port = htons(MC_PORT);
  multicastLength = sizeof(multicast_address);
  multicast_address.sin_addr.s_addr = inet_addr(MC_GROUP);

  //Setup timeout
  //https://stackoverflow.com/questions/13547721/udp-socket-set-timeout
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(multicast_descriptor, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
      perror("Error");
    }

  sendto(multicast_descriptor, multicast_message, 2*sizeof(unsigned char), 0, (struct sockaddr *) &multicast_address, multicastLength);

  //Retrieve response
  rc = recvfrom(multicast_descriptor, &multicast_response, 4*sizeof(unsigned char), 0, (struct sockaddr*) &multicast_address, &multicastLength);
  if((rc <= 0)){
    printf("No response from multicast, reading file\n");
    retrieveServerFromFile();
  }else{
    unsigned int networkPort;
    unsigned short portFinal;

    memcpy(&networkPort, &(multicast_response[2]), 2);

    portFinal = ntohs(networkPort); 

    if(reconnectSocket(inet_ntoa(multicast_address.sin_addr), portFinal) == -1){
      printf("Server gave invalid network info, exiting\n");
      exit(EXIT_FAILURE);
    };

    sendReconnect();
  }
}
