#define _XOPEN_SOURCE 600 // needed to add this so that I was no longer erroring on the addrinfo structs defined in netdb.h

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAXNAMELENGTH 256   // The maximum name length allowed for a station or platform
#define MAXBACKLOG 16       // The maximum allowable client connections to be in the backlog
#define MAXCONNECTIONS 1024 // The maximum allowable amount of connections from one station to another (Max in transperth files in 947 connections)
#define MAXLINESIZE 256     // The maximum allowable line size for one line in the timetable files when they are being parsed
#define MAXTCPDATASIZE 2048 // The maximum allowable amount of data being read in at one time for the incoming TCP connection
#define MAXUDPDATASIZE 2048 // The maximum allowable amount of data being read in at one time from an incoming UDP packet
#define TIMEOUT 3           // The allowable wait time for UDP routes to return before we time it out and become sad

struct Time {
    int hour;
    int minute;
};

struct TimetableEntry {
    struct Time departureTime;
    char lineOrBus[MAXNAMELENGTH];
    char departurePlatform[MAXNAMELENGTH];
    struct Time arrivalTime;
    char destinationStation[MAXNAMELENGTH];
};

struct Timetable {
    int numEntries;
    struct TimetableEntry entries[MAXCONNECTIONS];
};

struct Dictionary {
    int numberOfConections;
    int *udpPort;
    char **stationName;
};

struct tcpResponseStorage {
    unsigned int *initTime;
    bool *isUsed;
    int *packetsRecieved;
    char *initialTripMethod;
    char *initialTripPlatform;
    char *finalDestination;
    int *initialTripHour;
    int *initialTripMin;
    int *finalHour;
    int *finalMin;
} tcpResponses[FD_SETSIZE];

// ------------------------------------------------------------------------------------------------------------------------------------

void parseTimetableData(struct Timetable *timetable, const char *stationName, int stationNameLen, char *fileNameReturn) {
    int fileNameLen = 8 + stationNameLen;
    char fileName[fileNameLen];
    memset(fileName, 0, (size_t)fileNameLen * sizeof(char));
    fileName[0] = '.';
    fileName[1] = '/';
    fileName[2] = 't';
    fileName[3] = 't';
    fileName[4] = '/';
    fileName[5] = 't';
    fileName[6] = 't';
    fileName[7] = '-';
    strcat(fileName, stationName);

    FILE *fp = fopen(fileName, "r");

    char line[MAXLINESIZE];
    memset(line, 0, sizeof(line));
    char *val;

    fgets(line, sizeof(line), fp); // To skip over the station name, longtitude, and latitude sections of the timetable.
                                   // No error checking needed and not using lat or long.

    while (fgets(line, sizeof(line), fp) != NULL) {

        // Get the departure time
        val = strtok(line, ",");
        char hour[3];
        char min[3];
        hour[0] = val[0];
        hour[1] = val[1];
        hour[2] = '\n';
        min[0] = val[3];
        min[1] = val[4];
        min[2] = '\n';
        timetable->entries[timetable->numEntries].departureTime.hour = atoi(hour);
        timetable->entries[timetable->numEntries].departureTime.minute = atoi(min);

        // Get the bus or train line
        val = strtok(NULL, ",");
        strcpy(timetable->entries[timetable->numEntries].lineOrBus, val);
        timetable->entries[timetable->numEntries].lineOrBus[strlen(val)] = '\n';

        // Get the departure platform
        val = strtok(NULL, ",");
        strcpy(timetable->entries[timetable->numEntries].departurePlatform, val);
        timetable->entries[timetable->numEntries].departurePlatform[strlen(val)] = '\n';

        // Get the arrival time
        val = strtok(NULL, ",");
        hour[0] = val[0];
        hour[1] = val[1];
        min[0] = val[3];
        min[1] = val[4];
        timetable->entries[timetable->numEntries].arrivalTime.hour = atoi(hour);
        timetable->entries[timetable->numEntries].arrivalTime.minute = atoi(min);

        // Get the destination station
        val = strtok(NULL, ",");
        strcpy(timetable->entries[timetable->numEntries].destinationStation, val);
        timetable->numEntries += 1;
    }

    fclose(fp);
    fileNameReturn = strdup(fileName);
}

/**
 * Returns the socket fd of the CLIENT trying to connect
 */
int acceptNewConnection(int socketBeingConnectedTo, socklen_t *clientAdrLen, struct sockaddr_storage *clientAdr) {
    *clientAdrLen = sizeof(*clientAdr);
    return accept(socketBeingConnectedTo, (struct sockaddr *)clientAdr, clientAdrLen);
}

/**
 * This function takes in a timetable and the name of a station and checks
 * throughout the entire timetable to see if a connection exists to the given
 * station after a given time. This function will return an int indicating the
 * index of the entry in the timetable if a station is connected after the given
 * time. If none could be found it returns -1.
 */
int findNextDeparture(struct Timetable *tt, char *stationName, struct Time *time) {
    bool isFound = false;
    int i = 0;
    while (!isFound && i < tt->numEntries) {
        if ((strcmp(stationName, tt->entries[i].destinationStation) == 0) &&
            ((time->hour == tt->entries[i].departureTime.hour && time->minute < tt->entries[i].departureTime.minute) ||
             time->hour < tt->entries[i].departureTime.hour)) {
            isFound = true;
            i++;
            return i - 1;
        }
        i++;
    }
    return -1;
}

void sendTCPResponse(int clientSocketFD, char *transportMethod, char *departurePlatform, char *destinationStation, int departHour, int departMin,
                     int arriveHour, int arriveMin) {
    int totalMsgLen;
    char dHourBuf[12], dMinBuf[12], aHourBuf[12], aMinBuf[12];
    if (departHour < 10) {
        sprintf(dHourBuf, "0%i", departHour);
    } else {
        sprintf(dHourBuf, "%i", departHour);
    }
    if (departMin < 10) {
        sprintf(dMinBuf, "0%i", departMin);
    } else {
        sprintf(dMinBuf, "%i", departMin);
    }
    if (arriveHour < 10) {
        sprintf(aHourBuf, "0%i", arriveHour);
    } else {
        sprintf(aHourBuf, "%i", arriveHour);
    }
    if (arriveMin < 10) {
        sprintf(aMinBuf, "0%i", arriveMin);
    } else {
        sprintf(aMinBuf, "%i", arriveMin);
    }
    totalMsgLen = 143 + strlen(dHourBuf) + strlen(dMinBuf) + strlen(aHourBuf) + strlen(aMinBuf) + strlen(departurePlatform) +
                  strlen(destinationStation) + strlen(transportMethod);
    // Preset HTTP reply (HTTP/1.1 200 OK...)
    // + time strings +
    // departure platform name + bus or line name
    // destination name

    char buffer[totalMsgLen];
    sprintf(buffer,
            "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: "
            "Closed\n\n<html>\n<body><h1>Departing from %s at %s:%s on %s. Arriving at "
            "%s at %s:%s.</h1>\n</body>\n</html>",
            departurePlatform, dHourBuf, dMinBuf, transportMethod, destinationStation, aHourBuf, aMinBuf);
    if (send(clientSocketFD, buffer, totalMsgLen * sizeof(char), 0) == -1) {
        perror("Error while sending TCP response.");
    }
}

void sendNoConnectionsTCPResponse(int clientSocketFD, char *sourceStation, char *destStation, int departHour, int departMin) {
    int totalMsgLen;
    char dHourBuf[12], dMinBuf[12];
    if (departHour < 10) {
        sprintf(dHourBuf, "0%i", departHour);
    } else {
        sprintf(dHourBuf, "%i", departHour);
    }
    if (departMin < 10) {
        sprintf(dMinBuf, "0%i", departHour);
    } else {
        sprintf(dMinBuf, "%i", departMin);
    }

    totalMsgLen = 171 + strlen(dHourBuf) + strlen(dMinBuf) + strlen(sourceStation) + strlen(destStation);
    char buffer[totalMsgLen];
    sprintf(buffer,
            "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: Closed\n\n<html>\n"
            "<body><h1>Sorry! There are no connections available from %s to %s today after %s:%s!</h1>\n</body>\n</html>",
            sourceStation, destStation, dHourBuf, dMinBuf);
    send(clientSocketFD, buffer, totalMsgLen * sizeof(char), 0);
}

/**
 * The type 0 UDP packet is used to find the shortest path to the destination station. Contains metadata needed to successfully route through the
 * network efficiently Returns 1 if sent successfully, otherwise -1. Type 0 UDP packet format:
 * {SourcePort,DestinationPort,PacketType,InitialSourcePort,FinalDestinationName,ClientSocketFD,InitialHour,InitialMin,InitialBus,InitialPlatform,CurrPathHour,CurrPathMin,Path1|Path2|...}
 * E.g. "4003,4009,0,4001,North_Terminus,5,30,Bus_30,Platform_3,13,45,4003|4006|4018..."
 */
int sendType0UDPPacket(char *msg, int destPort, int udpPortFD) {
    struct sockaddr_in destAddr;
    int sizeOfMsgContents = strlen(msg) * sizeof(char);

    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    destAddr.sin_port = htons(destPort);
    memset(destAddr.sin_zero, 0, sizeof(destAddr.sin_zero));

    if (sendto(udpPortFD, msg, sizeOfMsgContents, 0, (struct sockaddr *)&destAddr, sizeof(struct sockaddr_storage)) == -1) {
        perror("Error while sending type 0 UDP packet.");
        return -1;
    }
    return 1;
}

/**
 * The type 1 UDP packet is used for one station to request anothers name.
 * Returns 1 if sent successfully, otherwise -1.
 * Type 1 UDP packet format:
 * {SourcePort,DestinationPort,PacketType}
 * E.g. "4001,4004,1"
 */
int sendType1UDPPacket(const char *sourcePort, const char *destPort, int udpPortFD) {
    int sizeOfBufferContents = (strlen(sourcePort) + strlen(destPort) + 4) * sizeof(char);
    char *buffer = calloc(sizeOfBufferContents / sizeof(char), sizeof(char));
    strcpy(buffer, sourcePort);
    buffer[strlen(sourcePort)] = ',';
    strcat(buffer, destPort);
    buffer[strlen(sourcePort) + strlen(destPort) + 1] = ',';
    buffer[strlen(sourcePort) + strlen(destPort) + 2] = '1';
    buffer[strlen(sourcePort) + strlen(destPort) + 3] = '\n';

    struct sockaddr_in destAddr;

    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    destAddr.sin_port = htons(atoi(destPort));
    memset(destAddr.sin_zero, 0, sizeof(destAddr.sin_zero));

    int nbytes = 0;

    if ((nbytes = sendto(udpPortFD, buffer, sizeOfBufferContents, 0, (struct sockaddr *)&destAddr, sizeof(struct sockaddr_storage))) == -1) {
        perror("Error while sending type 1 UDP packet.");
        return -1;
    }
    free(buffer);
    return 1;
}

/**
 * The type 2 UDP packet is used for a station to respond to anothers request for its name. I.e. sending its name back in response to a type 1 packet.
 * Returns 1 if sent successfully, otherwise -1.
 * Type 2 UDP packet format:
 * {SourcePort,DestinationPort,PacketType,MyName}
 * E.g. "4004,4001,2,South_Busport"
 */
int sendType2UDPPacket(const char *sourcePort, char *destPort, const char *myName, int udpPortFD) {
    int sizeOfBufferContents = (strlen(sourcePort) + strlen(destPort) + strlen(myName) + 5) * sizeof(char);
    char *buffer = calloc(sizeOfBufferContents / sizeof(char), sizeof(char));
    strcpy(buffer, sourcePort);
    buffer[strlen(sourcePort)] = ',';
    strcat(buffer, destPort);
    buffer[strlen(sourcePort) + strlen(destPort) + 1] = ',';
    buffer[strlen(sourcePort) + strlen(destPort) + 2] = '2';
    buffer[strlen(sourcePort) + strlen(destPort) + 3] = ',';
    strcat(buffer, myName);
    buffer[strlen(sourcePort) + strlen(destPort) + strlen(myName) + 4] = '\n';

    struct sockaddr_in destAddr;

    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    destAddr.sin_port = htons(atoi(destPort));
    memset(destAddr.sin_zero, 0, sizeof(destAddr.sin_zero));

    int nbytes = 0;

    if ((nbytes = sendto(udpPortFD, buffer, sizeOfBufferContents, 0, (struct sockaddr *)&destAddr, sizeof(struct sockaddr_storage))) == -1) {
        perror("Error while sending type 2 UDP packet.");
        return -1;
    }
    free(buffer);
    return 1;
}

/**
 * The type 3 UDP packet is used once a path from the initial source station to the destination station has been found. It sends back the needed
 * data for the initial source station to send a TCP response back to the client.
 * Returns 1 if sent successfully, otherwise -1.
 * Type 3 UDP packet format:
 * {SourcePort,DestinationPort,PacketType,ClientSocketFD,InitialHour,InitialMin,InitialBus,InitialPlatform,ArivalHour,ArivalMin,ArivalStationName}
 * E.g. "4018,4001,3,5,30,Bus_30,Platform_3,14,30,North_Terminus"
 */
int sendType3UDPPacket(char *myPort, int destPort, int clientSocketFD, int initialTripHour, int initialTripMin, char *initialTripMethod,
                       char *initialPlatform, int arivalHour, int arivalMin, char *arivalStation, int udpPortFD) {
    char dHourBuf[12], dMinBuf[12], aHourBuf[12], aMinBuf[12], clientSocketFDString[12], destPortString[12];
    sprintf(dHourBuf, "%i,", initialTripHour);
    sprintf(dMinBuf, "%i,", initialTripMin);
    sprintf(aHourBuf, "%i,", arivalHour);
    sprintf(aMinBuf, "%i,", arivalMin);
    sprintf(clientSocketFDString, "%i,", clientSocketFD);
    sprintf(destPortString, "%i,", destPort);
    int curMsgLen = 0;

    char *buffer = calloc(MAXUDPDATASIZE, sizeof(char));

    sprintf(buffer, "%s,%s3,%s%s%s%s,%s,%s%s%s", myPort, destPortString, clientSocketFDString, dHourBuf, dMinBuf, initialTripMethod, initialPlatform,
            aHourBuf, aMinBuf, arivalStation);
    curMsgLen = strlen(buffer);
    buffer[curMsgLen] = '\n';

    struct sockaddr_in destAddr;

    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    destAddr.sin_port = htons(destPort);
    memset(destAddr.sin_zero, 0, sizeof(destAddr.sin_zero));

    if (sendto(udpPortFD, buffer, curMsgLen * sizeof(char), 0, (struct sockaddr *)&destAddr, sizeof(struct sockaddr_storage)) == -1) {
        perror("Error while sending type 3 UDP packet.");
        return -1;
    }
    free(buffer);
    return 1;
}

/**
 * Returns a 1 if correct response has been sent and socket has been closed, otherwise -1.
 */
int handleTCPConnection(int clientSocketFD, char *myPort, struct Timetable *tt, struct Dictionary *neighbourUDPPorts, int udpPortFD) {
    char buf[MAXTCPDATASIZE];
    memset(buf, 0, sizeof(buf));
    int nbytes;
    if ((nbytes = recv(clientSocketFD, buf, sizeof(buf) - sizeof(char), 0)) <=
        0) {               // We just care about the first line beacuse that will have the GET
                           // /?to={destination_Station}
        if (nbytes != 0) { // Client did not close the connection, an error occured
            perror("recv in handleTCPConnection failed");
        }
    }
    buf[MAXTCPDATASIZE] = '\n';

    char faviconFucker[] = "favicon";
    if (strstr(buf, faviconFucker) != NULL) { // If this is a favicon request just discard it, we don't deal with that around here.
        shutdown(clientSocketFD, SHUT_RDWR);
        close(clientSocketFD);
        return 1;
    }

    char needle1[2] = "=";
    char needle2[] = {" HTTP"};

    char *startOfGETReq = strstr(buf, needle1);
    char *endOfGETReq = strstr(buf, needle2);
    if (startOfGETReq == NULL || endOfGETReq == NULL) {
        return 0;
    }
    startOfGETReq++;
    size_t lenOfReq = (size_t)(endOfGETReq - startOfGETReq);
    char *destinationStationName;
    destinationStationName = (char *)calloc((lenOfReq + 1), sizeof(char));
    for (int i = 0; i < lenOfReq; i++) {
        destinationStationName[i] = startOfGETReq[i];
    }
    destinationStationName[lenOfReq] = '\n';

    time_t timeSinceEpoch = time(NULL);
    struct tm *timeInfo = malloc(sizeof(struct tm));
    timeInfo = localtime(&timeSinceEpoch);
    struct Time localTime;
    localTime.hour = timeInfo->tm_hour;
    localTime.minute = timeInfo->tm_min;

    int tmp = -1;
    if ((tmp = findNextDeparture(tt, destinationStationName, &localTime)) != -1) {
        sendTCPResponse(clientSocketFD, tt->entries[tmp].lineOrBus, tt->entries[tmp].departurePlatform, destinationStationName,
                        tt->entries[tmp].departureTime.hour, tt->entries[tmp].departureTime.minute, tt->entries[tmp].arrivalTime.hour,
                        tt->entries[tmp].arrivalTime.minute);

        shutdown(clientSocketFD, SHUT_RDWR);
        close(clientSocketFD);
        return 1;
    } else {
        *(tcpResponses[clientSocketFD].packetsRecieved) = 0;
        *(tcpResponses[clientSocketFD].isUsed) = true;
        *(tcpResponses[clientSocketFD].finalHour) = 30;
        *(tcpResponses[clientSocketFD].finalMin) = 70;
        *(tcpResponses[clientSocketFD].initTime) = time(NULL) + TIMEOUT;
        strcpy(tcpResponses[clientSocketFD].finalDestination, destinationStationName);
        int msgsSent = 0;
        for (int i = 0; i < neighbourUDPPorts->numberOfConections; i++) {
            char *msg = calloc(MAXUDPDATASIZE, sizeof(char));
            char clientSocketFDStr[12];
            sprintf(clientSocketFDStr, "%i,", clientSocketFD);
            sprintf(msg, "%s,9999,0,%s,", myPort, myPort);
            int curMsgLen = strlen(msg);
            strcat(msg, destinationStationName);
            curMsgLen = strlen(msg);
            msg[curMsgLen - 1] = ',';
            strcat(msg, clientSocketFDStr);
            curMsgLen = strlen(msg);

            char toPort[12];
            sprintf(toPort, "%i", neighbourUDPPorts->udpPort[i]);
            for (int i = 5; i < 9; i++) {
                msg[i] = toPort[i - 5];
            }
            if ((tmp = findNextDeparture(tt, neighbourUDPPorts->stationName[i], &localTime)) != -1) {
                char initHour[12];
                char initMin[12];
                sprintf(initHour, "%i,", tt->entries[tmp].departureTime.hour);
                sprintf(initMin, "%i,", tt->entries[tmp].departureTime.minute);
                char finalHour[12];
                char finalMin[12];
                sprintf(finalHour, "%i,", tt->entries[tmp].arrivalTime.hour);
                sprintf(finalMin, "%i,", tt->entries[tmp].arrivalTime.minute);

                strcat(msg, initHour);
                strcat(msg, initMin);
                curMsgLen = strlen(msg);

                strcat(msg, tt->entries[tmp].lineOrBus);
                curMsgLen = strlen(msg);
                msg[curMsgLen - 1] = ',';

                strcat(msg, tt->entries[tmp].departurePlatform);
                curMsgLen = strlen(msg);
                msg[curMsgLen - 1] = ',';

                strcat(msg, finalHour);
                strcat(msg, finalMin);
                strcat(msg, myPort);
                curMsgLen = strlen(msg);
                msg[curMsgLen] = '|';
                sendType0UDPPacket(msg, neighbourUDPPorts->udpPort[i], udpPortFD);
                msgsSent++;
            }
        }
        if (msgsSent == 0) {
            sendNoConnectionsTCPResponse(clientSocketFD, "current station", destinationStationName, localTime.hour, localTime.minute);
            *(tcpResponses[clientSocketFD].packetsRecieved) = 0;
            *(tcpResponses[clientSocketFD].isUsed) = false;
            *(tcpResponses[clientSocketFD].finalHour) = 30;
            *(tcpResponses[clientSocketFD].finalMin) = 70;
            return 1;
        }
        return -1;
    }
    return -1;
}

int handleType0Packet(char *msg, struct Dictionary *neighbourUDPPorts, struct Timetable *tt, int udpPortFD) {
    char *val;
    char *finalDest;
    char *finalDest2;
    char *initialTripMethod;
    char *initialTripPlatform;
    char *myPort;
    char *destPort;
    int clientsocketFD;
    int initialSrcPort;
    int initialTripHour;
    int initialTripMin;
    int currentTripHour;
    int currentTripMin;

    bool dontSendTo[neighbourUDPPorts->numberOfConections];
    for (int i = 0; i < neighbourUDPPorts->numberOfConections; i++) {
        dontSendTo[i] = false;
    }

    char *modifiedMsg = strdup(msg);

    val = strtok(msg, ",");  // Source Port
    val = strtok(NULL, ","); // Dest Port
    destPort = strdup(val);
    myPort = strdup(val);
    val = strtok(NULL, ","); // Packet Type

    val = strtok(NULL, ",");
    initialSrcPort = atoi(val);

    val = strtok(NULL, ",");
    finalDest = strdup(val);
    finalDest2 = calloc(strlen(val) + 1, sizeof(char));
    strcpy(finalDest2, val);
    finalDest2[strlen(val)] = '\n';

    val = strtok(NULL, ",");
    clientsocketFD = atoi(val);

    val = strtok(NULL, ",");
    initialTripHour = atoi(val);

    val = strtok(NULL, ",");
    initialTripMin = atoi(val);

    val = strtok(NULL, ",");
    initialTripMethod = strdup(val);

    val = strtok(NULL, ",");
    initialTripPlatform = strdup(val);

    val = strtok(NULL, ",");
    currentTripHour = atoi(val);

    val = strtok(NULL, ",");
    currentTripMin = atoi(val);

    struct Time time;
    time.hour = currentTripHour;
    time.minute = currentTripMin;

    for (int i = 0; i < neighbourUDPPorts->numberOfConections; i++) {
        if (strcmp(finalDest2, neighbourUDPPorts->stationName[i]) == 0) {
            int tmp = -1;
            if ((tmp = findNextDeparture(tt, finalDest2, &time)) != -1) {
                sendType3UDPPacket(myPort, initialSrcPort, clientsocketFD, initialTripHour, initialTripMin, initialTripMethod, initialTripPlatform,
                                   tt->entries[tmp].arrivalTime.hour, tt->entries[tmp].arrivalTime.minute, finalDest, udpPortFD);
                return -6;
            } else {
                dontSendTo[i] = true;
                break;
            }
        }
    }

    bool avail = false; // This is used to ensure that there is AT LEAST one connected station we can send a packet to

    for (int i = 0; i < neighbourUDPPorts->numberOfConections; i++) {
        char temp[5];
        sprintf(temp, "%i|", neighbourUDPPorts->udpPort[i]);
        if (strstr(modifiedMsg, temp) != NULL) {
            dontSendTo[i] = true;
        } else {
            avail = true;
        }
    }

    if (!avail) {
        return -20;
    }

    char *delim = "|";
    strcat(modifiedMsg, destPort);
    strcat(modifiedMsg, delim);

    for (int i = 0; i < neighbourUDPPorts->numberOfConections; i++) {
        if (!dontSendTo[i]) {
            char buf[12];
            sprintf(buf, "%i", neighbourUDPPorts->udpPort[i]);
            memcpy(&modifiedMsg[5], buf, sizeof(char) * 4); // Assumes all ports are a combination of 4 numbers (again)

            sendType0UDPPacket(modifiedMsg, neighbourUDPPorts->udpPort[i], udpPortFD);
        }
    }
    return -6;
}

int handleUDPDataRecieved(int udpSocketFd, const char *myPort, const char *myName, struct Dictionary *neighbourUDPPorts, struct Timetable *tt) {
    char *buffer = calloc(MAXUDPDATASIZE, sizeof(char));
    int nbytes = 0;
    struct sockaddr_storage senderAddress;
    socklen_t senderAddressLength;
    if ((nbytes = recvfrom(udpSocketFd, buffer, MAXUDPDATASIZE * sizeof(char), 0, (struct sockaddr *)&senderAddress, &senderAddressLength)) == -1) {
        perror("Error in recvfrom in handleUDPDataRecieved.");
    }
    char *msgDupe = strdup(buffer);
    char *val;
    char *sourcePort;
    char *destPort;
    int packetType;

    // Parse out the incoming Source Port component (senders port)
    val = strtok(buffer, ",");
    sourcePort = strdup(val);

    // Parse out the incoming Destination Port component (my port)
    val = strtok(NULL, ",");
    destPort = strdup(val);

    // Parse out the Packet Type component
    val = strtok(NULL, ",");
    packetType = atoi(val);

    int clientSocketFD;
    int initialTripHour;
    int initialTripMin;
    int finalHour;
    int finalMin;
    char *initialTripMethod;
    char *initialTripPlatform;
    char *finalDestination;

    switch (packetType) {

    case 0:
        return handleType0Packet(msgDupe, neighbourUDPPorts, tt, udpSocketFd);
        break;

    case 1: // The port who sent us the data is asking what our name is
        sendType2UDPPacket(myPort, sourcePort, myName, udpSocketFd);
        return -1;
        break;

    case 2: // We recieved a response to our type 1 udp packet and log their name
        val = strtok(NULL, ",");
        for (int i = 0; i < neighbourUDPPorts->numberOfConections; i++) {
            if (neighbourUDPPorts->udpPort[i] == atoi(sourcePort)) {
                neighbourUDPPorts->stationName[i] = strdup(val);
                return -2;
                break;
            }
        }
        return -2;
        break;

    case 3: // We recieved a response that contains the final information we need to relay to the client.
        val = strtok(NULL, ",");
        clientSocketFD = atoi(val);

        if ((*(tcpResponses[clientSocketFD].isUsed) == false) ||
            time(NULL) > *(tcpResponses[clientSocketFD].initTime)) { // Don't take packets if it comes after we've stopped recieving packets
            return -10;
            break;
        }

        val = strtok(NULL, ",");
        initialTripHour = atoi(val);

        val = strtok(NULL, ",");
        initialTripMin = atoi(val);

        val = strtok(NULL, ",");
        initialTripMethod = strdup(val);

        val = strtok(NULL, ",");
        initialTripPlatform = strdup(val);

        val = strtok(NULL, ",");
        finalHour = atoi(val);

        val = strtok(NULL, ",");
        finalMin = atoi(val);

        val = strtok(NULL, ",");
        finalDestination = strdup(val);

        *(tcpResponses[clientSocketFD].packetsRecieved) += 1;

        if ((*(tcpResponses[clientSocketFD].finalHour) > finalHour ||
             (*(tcpResponses[clientSocketFD].finalHour) == finalHour && *(tcpResponses[clientSocketFD].finalMin) > finalMin))) {

            memset(tcpResponses[clientSocketFD].finalDestination, 0, sizeof(char) * MAXNAMELENGTH);
            memset(tcpResponses[clientSocketFD].initialTripMethod, 0, sizeof(char) * MAXNAMELENGTH);
            memset(tcpResponses[clientSocketFD].initialTripPlatform, 0, sizeof(char) * MAXNAMELENGTH);
            for (int i = 0; i < strlen(finalDestination); i++) {
                tcpResponses[clientSocketFD].finalDestination[i] = finalDestination[i];
            }
            for (int i = 0; i < strlen(initialTripMethod); i++) {
                tcpResponses[clientSocketFD].initialTripMethod[i] = initialTripMethod[i];
            }
            for (int i = 0; i < strlen(initialTripPlatform); i++) {
                tcpResponses[clientSocketFD].initialTripPlatform[i] = initialTripPlatform[i];
            }

            *(tcpResponses[clientSocketFD].finalHour) = finalHour;
            *(tcpResponses[clientSocketFD].finalMin) = finalMin;
            *(tcpResponses[clientSocketFD].initialTripHour) = initialTripHour;
            *(tcpResponses[clientSocketFD].initialTripMin) = initialTripMin;
            return -11;
            break;
        } else {
            return -12;
            break;
        }
        break;

    default:
        free(sourcePort);
        free(destPort);
        return -5;
        break;
    }
}

// ---------------------------------------------------- MAIN FUNCTION -------------------------------------------------------------
int main(int argc, char const *argv[]) {
    if (argc < 5) {
        printf("Must have at least 4 arguments.\nUsage: ./station {station_name} "
               "{station_TCP_port_number} {station_UDP_port_number} "
               "{Connected_station_UDP_port_number}...\n");
        exit(EXIT_FAILURE);
    }
    char *timetableFileName = calloc(8 + strlen(argv[1]), sizeof(char));
    int maxSocketSoFar = 0;
    int yes = 1;
    struct Timetable timetable;
    memset(&timetable, 0, sizeof(timetable));
    memset(timetable.entries, 0, sizeof(struct TimetableEntry) * MAXCONNECTIONS);
    parseTimetableData(&timetable, argv[1], (int)strlen(argv[1]), timetableFileName);

    // -------------------------- Initialize this station's TCP socket for listening to incoming connections --------------------------
    int tcpSocketFD;
    struct sockaddr_in serverAddressTCP;

    if ((tcpSocketFD = socket(AF_INET, SOCK_STREAM, 0)) == 0) { // Initialize the socket
        perror("tcpSocketFD initialization failed.");
        exit(EXIT_FAILURE);
    }

    setsockopt(tcpSocketFD, SOL_SOCKET, SO_REUSEADDR, &yes,
               sizeof(yes)); // to ensure that "address already in use" doesn't appear

    serverAddressTCP.sin_family = AF_INET;
    serverAddressTCP.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddressTCP.sin_port = htons(atoi(argv[2]));
    memset(serverAddressTCP.sin_zero, 0, sizeof(serverAddressTCP.sin_zero));

    if (bind(tcpSocketFD, (struct sockaddr *)&serverAddressTCP,
             sizeof(struct sockaddr)) < 0) { // Bind the socket to the correct port
        perror("Binding of tcpSocket failed.");
        exit(EXIT_FAILURE);
    }

    if (listen(tcpSocketFD, MAXBACKLOG) != 0) { // Listen on the socket for incoming connections
        perror("Listening of tcpSocket failed.");
        exit(EXIT_FAILURE);
    }

    // -------------------- Initialize this station's UDP socket -------------------------------------------------------------------
    int udpSocketFD;
    struct sockaddr_in serverAddressUDP;

    if ((udpSocketFD = socket(AF_INET, SOCK_DGRAM, 0)) == 0) { // Initialize the socket
        perror("udpSocketFD initialization failed.");
        exit(EXIT_FAILURE);
    }

    setsockopt(udpSocketFD, SOL_SOCKET, SO_REUSEADDR, &yes,
               sizeof(int)); // to ensure that "address already in use" doesn't appear

    serverAddressUDP.sin_family = AF_INET;
    serverAddressUDP.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddressUDP.sin_port = htons(atoi(argv[3]));
    memset(serverAddressUDP.sin_zero, 0, sizeof(serverAddressUDP.sin_zero));

    if (bind(udpSocketFD, (struct sockaddr *)&serverAddressUDP,
             sizeof(struct sockaddr)) < 0) { // Bind the socket to the correct port
        perror("Binding of udpSocket failed.");
        exit(EXIT_FAILURE);
    }
    // You do not need to call listen() on a udp connection as they will just
    // recieve things out of the blue on this socket

    // WAIT so that other servers can initialize before we start pestering them
    unsigned int waitTime = time(NULL) + 2;
    while ((time(NULL)) <= waitTime);

    // Record all port numbers of neighbouring stations and send out udp packets asking for them to send their station name back
    struct Dictionary neighbourUDPPorts;
    neighbourUDPPorts.udpPort = calloc(argc - 4, sizeof(int));
    neighbourUDPPorts.stationName = calloc(argc - 4, sizeof(char *));
    neighbourUDPPorts.numberOfConections = argc - 4;
    for (int i = 4; i < argc; i++) {
        neighbourUDPPorts.udpPort[i - 4] = atoi(argv[i]);
        sendType1UDPPacket(argv[3], argv[i], udpSocketFD);
    }

    // -------------------- Set up the FD_SET to use for select() ----------------------------------------------------------------
    int timetableFD = open(timetableFileName, 0, O_RDONLY);
    fd_set currentSockets, readySockets;
    // Initialize the current set of sockets
    FD_ZERO(&currentSockets);
    FD_SET(tcpSocketFD, &currentSockets);
    FD_SET(udpSocketFD, &currentSockets);
    FD_SET(timetableFD, &currentSockets);
    if (tcpSocketFD > udpSocketFD) { // Set the initial maxSocketSoFar
        maxSocketSoFar = tcpSocketFD;
    } else if (udpSocketFD > timetableFD) {
        maxSocketSoFar = udpSocketFD;
    } else {
        maxSocketSoFar = timetableFD;
    }
    int clientSocketFD = -1;
    struct sockaddr_storage clientAddress;
    socklen_t clientAddressLength;

    for (int i = 0; i < FD_SETSIZE; i++) {
        tcpResponses[i].initTime = calloc(1, sizeof(unsigned int));
        tcpResponses[i].isUsed = calloc(1, sizeof(bool));
        tcpResponses[i].packetsRecieved = calloc(1, sizeof(int));
        tcpResponses[i].finalDestination = calloc(MAXNAMELENGTH, sizeof(char));
        tcpResponses[i].initialTripMethod = calloc(MAXNAMELENGTH, sizeof(char));
        tcpResponses[i].initialTripPlatform = calloc(MAXNAMELENGTH, sizeof(char));
        tcpResponses[i].initialTripHour = calloc(1, sizeof(int));
        tcpResponses[i].initialTripMin = calloc(1, sizeof(int));
        tcpResponses[i].finalHour = calloc(1, sizeof(int));
        tcpResponses[i].finalMin = calloc(1, sizeof(int));
    }

    struct timeval selectTimeout;

    while (true) {
        readySockets = currentSockets;

        for (int i = 0; i <= maxSocketSoFar; i++) {
            if (*(tcpResponses[i].isUsed) == true) {
                if (time(NULL) >= *(tcpResponses[i].initTime)) {
                    if (*(tcpResponses[i].packetsRecieved) > 0) {
                        sendTCPResponse(i, tcpResponses[i].initialTripMethod, tcpResponses[i].initialTripPlatform, tcpResponses[i].finalDestination,
                                        *(tcpResponses[i].initialTripHour), *(tcpResponses[i].initialTripMin), *(tcpResponses[i].finalHour),
                                        *(tcpResponses[i].finalMin));
                    } else {
                        time_t timeSinceEpoch = time(NULL);
                        struct tm *timeInfo = malloc(sizeof(struct tm));
                        timeInfo = localtime(&timeSinceEpoch);
                        sendNoConnectionsTCPResponse(i, strdup(argv[1]), tcpResponses[i].finalDestination, timeInfo->tm_hour, timeInfo->tm_min);
                    }
                    shutdown(clientSocketFD, SHUT_RDWR);
                    close(clientSocketFD);
                    FD_CLR(i, &currentSockets);

                    *(tcpResponses[i].finalHour) = 30;
                    *(tcpResponses[i].finalMin) = 70;
                    *(tcpResponses[i].packetsRecieved) = 0;
                    *(tcpResponses[i].isUsed) = false;
                    readySockets = currentSockets;
                }
            }
        }

        selectTimeout.tv_sec = 2;
        selectTimeout.tv_usec = 0;

        int retval;
        if ((retval = select(maxSocketSoFar + 1, &readySockets, NULL, NULL, &selectTimeout)) <=
            0) { // select(numberOfFDs, fdsetToRead, fdsetToWrite, fdSetExceptions, timeout)
            if (retval == 0) {
                continue;
            } else {
                perror("Select failed.");
            }
        } else {

            for (int i = 0; i <= maxSocketSoFar; i++) {
                if (FD_ISSET(i, &readySockets)) {
                    if (i == tcpSocketFD) { // This is if the TCP socket is ready and is going to recieve a tcp connection
                        if ((clientSocketFD = acceptNewConnection(tcpSocketFD, &clientAddressLength, &clientAddress)) == -1) {
                        }
                        FD_SET(clientSocketFD, &currentSockets);
                        if (clientSocketFD > maxSocketSoFar) {
                            maxSocketSoFar = clientSocketFD;
                        }
                    } else if (i == udpSocketFD) { // This is if our UDP socket is ready to recieve some juicy data
                        int tmp;
                        tmp = handleUDPDataRecieved(udpSocketFD, argv[3], argv[1], &neighbourUDPPorts, &timetable);
                        if (tmp >= 0) {
                            FD_CLR(tmp, &currentSockets);
                        }
                    } else if (i == timetableFD) { // The timetable file has been updated and we need to re-parse out the data
                        parseTimetableData(&timetable, argv[1], (int)strlen(argv[1]), timetableFileName);
                    } else { // The client connection has sent data and we need to handle that shit
                        if (handleTCPConnection(i, strdup(argv[3]), &timetable, &neighbourUDPPorts, udpSocketFD) == 1) {
                            // If the response has already been sent and the socket was closed
                            FD_CLR(i, &currentSockets);
                        } // Else we wait for a UDP to be sent to us that has the result we need to send over the client socket via TCP
                    }
                }
            }
        }
    }
    return 0;
}