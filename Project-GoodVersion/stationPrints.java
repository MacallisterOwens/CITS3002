import java.net.*;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.channels.*;
import java.nio.charset.StandardCharsets;
import java.time.LocalTime;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Scanner;

/**
 * This class is a copy used for debugging
 */
public class station {

    public HashMap<Integer, String> neighbourUDPPorts = new HashMap<Integer, String>();
    public HashMap<SelectionKey, ClientSession> clients = new HashMap<SelectionKey, ClientSession>();
    public HashMap<Integer, ClientSession> clientsFD = new HashMap<Integer, ClientSession>();
    public LinkedList<TimetableEntry> timetable = new LinkedList<TimetableEntry>();
    public LinkedList<TCPResponseStorage> tcpResponses = new LinkedList<TCPResponseStorage>();

    public Selector selector;
    private String stationName;
    private int tcpPort;
    private int udpPort;

    public station(String stationName, int tcpPort, int udpPort) {
        this.stationName = stationName;
        this.tcpPort = tcpPort;
        this.udpPort = udpPort;
    }

    public void setStationName(String stationName) {
        this.stationName = stationName;
    }

    public void setTCPPort(int tcpPort) {
        this.tcpPort = tcpPort;
    }

    public void setUDPPort(int udpPort) {
        this.udpPort = udpPort;
    }

    public String getStationName() {
        return stationName;
    }

    public int getTCPPort() {
        return tcpPort;
    }

    public int getUDPPort() {
        return udpPort;
    }

    public void parseTimetableData() {
        StringBuilder ttFileName = new StringBuilder("./tt/tt-");
        ttFileName.append(stationName);
        File ttFile = new File(ttFileName.toString());

        try (Scanner scanner = new Scanner(ttFile)) {
            String delim = ",";
            scanner.useDelimiter(delim);

            scanner.next(); // Discard station name
            scanner.next(); // Discard lat
            scanner.nextLine(); // Discard long

            // Parse out the contents of the timetable
            while (scanner.hasNext()) {
                TimetableEntry tmp = new TimetableEntry();

                String[] departTime = scanner.next().split("[:]");
                tmp.setDepartHour(Integer.parseInt(departTime[0]));
                tmp.setDepartMin(Integer.parseInt(departTime[1]));
                tmp.setLineOrBus(scanner.next());
                tmp.setDeparturePlatform(scanner.next());
                String[] arrivalTime = scanner.next().split(":");
                tmp.setArriveHour(Integer.parseInt(arrivalTime[0]));
                tmp.setArriveMin(Integer.parseInt(arrivalTime[1]));
                tmp.setDestStation(scanner.nextLine().substring(1));
                timetable.offer(tmp);
            }
            scanner.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public TimetableEntry findNextDeparture(String stationName, int hour, int min) {
        for (TimetableEntry ttEntry : timetable) {
            if (ttEntry.getDestStation().equalsIgnoreCase(stationName)
                    && ((hour == ttEntry.getDepartHour() && min < ttEntry.getDepartMin())
                            || (hour < ttEntry.getDepartHour()))) {
                return ttEntry;
            }
        }
        return null;
    }

    public void sendTCPResponse(ClientSession cSession, String transportMethod, String departurePlatform,
            String destinationStation, int departHour, int departMin, int arriveHour, int arriveMin) {
        String dHour, dMin, aHour, aMin;
        if (departHour < 10)
            dHour = "0" + departHour;
        else
            dHour = "" + departHour;

        if (departMin < 10)
            dMin = "0" + departMin;
        else
            dMin = "" + departMin;

        if (arriveHour < 10)
            aHour = "0" + arriveHour;
        else
            aHour = "" + arriveHour;

        if (arriveMin < 10)
            aMin = "0" + arriveMin;
        else
            aMin = "" + arriveMin;

        String msg = "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: Closed\n\n<html>\n<body><h1>Departing from "
                + departurePlatform + " at " + dHour + ":" + dMin + " on " + transportMethod + ". Arriving at "
                + destinationStation + " at " + aHour + ":" + aMin + ".</h1>\n</body>\n</html>";
        ByteBuffer msgByteBuffer = ByteBuffer.wrap(msg.getBytes());
        try {
            cSession.getSocketChannel().write(msgByteBuffer);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public void sendNoConnectionsTCPResponse(ClientSession cSession) {
        String msg = "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: Closed\n\n<html>\n<body><h1>Sorry! There are no connections available from "
                + stationName + " to " + cSession.getTCPResponse().getFinalDestination() + " today after "
                + LocalTime.now().getHour() + ":" + LocalTime.now().getMinute() + "!</h1>\n</body>\n</html>";

        ByteBuffer msgByteBuffer = ByteBuffer.wrap(msg.getBytes());
        try {
            cSession.getSocketChannel().write(msgByteBuffer);
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    /**
     * The type 0 UDP packet is used to find the shortest path to the destination
     * station. Contains metadata needed to successfully route through the network
     * efficiently Returns 1 if sent successfully, otherwise -1. Type 0 UDP packet
     * format:
     * {SourcePort,DestinationPort,PacketType,InitialSourcePort,FinalDestinationName,ClientSocketFD,InitialHour,InitialMin,InitialBus,InitialPlatform,CurrPathHour,CurrPathMin,Path1|Path2|...}
     * E.g.
     * "4003,4009,0,4001,North_Terminus,5,30,Bus_30,Platform_3,13,45,4003|4006|4018..."
     */
    public int sendType0UDPPacket(String msg, int destPort, DatagramChannel udpChannel) {
        ByteBuffer msgByteBuffer = ByteBuffer.wrap(msg.getBytes());
        System.out.println(
                "JAVA " + "Port " + getUDPPort() + " is sending a type 0 udp packet to " + destPort + ". Msg: " + msg);
        try {
            udpChannel.send(msgByteBuffer, new InetSocketAddress("localhost", destPort));
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }
        return 1;
    }

    /**
     * The type 1 UDP packet is used for one station to request anothers name.
     * Returns 1 if sent successfully, otherwise -1. Type 1 UDP packet format:
     * {SourcePort,DestinationPort,PacketType} E.g. "4001,4004,1"
     */
    public int sendType1UDPPacket(DatagramChannel udpChannel, int destPort) {
        String msg = "" + getUDPPort() + "," + destPort + ",1";
        ByteBuffer msgByteBuffer = ByteBuffer.wrap(msg.getBytes());
        System.out.println(
                "JAVA " + "Port " + getUDPPort() + " is sending a type 1 udp packet to " + destPort + ". Msg: " + msg);
        try {
            udpChannel.send(msgByteBuffer, new InetSocketAddress("localhost", destPort));
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }
        return 1;
    }

    /**
     * The type 2 UDP packet is used for a station to respond to anothers request
     * for its name. I.e. sending its name back in response to a type 1 packet.
     * Returns 1 if sent successfully, otherwise -1. Type 2 UDP packet format:
     * {SourcePort,DestinationPort,PacketType,MyName} E.g.
     * "4004,4001,2,South_Busport"
     */
    public int sendType2UDPPacket(DatagramChannel udpChannel, int destPort) {
        String msg = "" + getUDPPort() + "," + destPort + ",2," + getStationName() + "\n";
        ByteBuffer msgByteBuffer = ByteBuffer.wrap(msg.getBytes());
        System.out.println(
                "JAVA " + "Port " + getUDPPort() + " is sending a type 2 udp packet to " + destPort + ". Msg: " + msg);
        try {
            udpChannel.send(msgByteBuffer, new InetSocketAddress("localhost", destPort));
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }
        return 1;
    }

    /**
     * The type 3 UDP packet is used once a path from the initial source station to
     * the destination station has been found. It sends back the needed data for the
     * initial source station to send a TCP response back to the client. Returns 1
     * if sent successfully, otherwise -1. Type 3 UDP packet format:
     * {SourcePort,DestinationPort,PacketType,ClientSocketFD,InitialHour,InitialMin,InitialBus,InitialPlatform,ArivalHour,ArivalMin,ArivalStationName}
     * E.g. "4018,4001,3,5,30,Bus_30,Platform_3,14,30,North_Terminus"
     */
    public int sendType3UDPPacket(DatagramChannel udpChannel, int destPort, int clientSocketFD, int initHour,
            int initMin, String initMethodOfTransport, String initPlatform, int arriveHour, int arriveMin,
            String arrivalStation) {
        String msg = "" + getUDPPort() + "," + destPort + ",3," + clientSocketFD + "," + initHour + "," + initMin + ","
                + initMethodOfTransport + "," + initPlatform + "," + arriveHour + "," + arriveMin + ","
                + arrivalStation;
        ByteBuffer msgByteBuffer = ByteBuffer.wrap(msg.getBytes());
        System.out.println(
                "JAVA " + "Port " + getUDPPort() + " is sending a type 3 udp packet to " + destPort + ". Msg: " + msg);
        try {
            udpChannel.send(msgByteBuffer, new InetSocketAddress("localhost", destPort));
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }
        return 1;
    }

    public int handleTCPConnection(ClientSession cSession, DatagramChannel udpChannel) {
        SocketChannel sChannel = cSession.getSocketChannel();
        ByteBuffer byteBuf = ByteBuffer.allocateDirect(2048);
        int nBytesRead = -1;

        try {
            nBytesRead = sChannel.read(byteBuf);
            if (nBytesRead <= 0) {
                System.out.println("Issue reading in data.");
                return -1;
            }

            byteBuf.flip();

            String msg = StandardCharsets.UTF_8.decode(byteBuf).toString();
            if (msg.contains("favicon")) { // If this is a favicon request just discard it, we don't deal with that
                                           // around here.
                System.out.println("JAVA " + "Ignore me, I was requesting a favicon...");
                return 1;
            }

            int startOfGetReq = msg.indexOf("=") + 1;
            int endOfGetReq = msg.indexOf(" HTTP");

            String destinationStationName = msg.substring(startOfGetReq, endOfGetReq);
            LocalTime localTime = LocalTime.now();
            int hour = localTime.getHour();
            int min = localTime.getMinute();

            TimetableEntry tmp = findNextDeparture(destinationStationName, hour, min);
            if (tmp != null) {
                sendTCPResponse(cSession, tmp.getLineOrBus(), tmp.getDeparturePlatform(), tmp.getDestStation(),
                        tmp.getDepartHour(), tmp.getDepartMin(), tmp.getArriveHour(), tmp.getArriveMin());
                return 1;
            } else {
                TCPResponseStorage tmpTCP = cSession.getTCPResponse();
                tmpTCP.setIsUsed(true);
                tmpTCP.resetPacketsRecieved();
                tmpTCP.setFinalHour(30);
                tmpTCP.setFinalMin(70);
                tmpTCP.setInitTime(LocalTime.now().toSecondOfDay() + 2);
                tmpTCP.setFinalDestination(destinationStationName);
                int msgsSent = 0;
                for (Integer portNum : neighbourUDPPorts.keySet()) {
                    TimetableEntry tmpttEntry = findNextDeparture(neighbourUDPPorts.get(portNum), hour, min);
                    if (tmpttEntry != null) {
                        String msgToSend = "" + getUDPPort() + "," + portNum + ",0," + getUDPPort() + ","
                                + destinationStationName + "," + cSession.getClientSocketFD() + ","
                                + tmpttEntry.getDepartHour() + "," + tmpttEntry.getDepartMin() + ","
                                + tmpttEntry.getLineOrBus() + "," + tmpttEntry.getDeparturePlatform() + ","
                                + tmpttEntry.getArriveHour() + "," + tmpttEntry.getArriveMin() + "," + getUDPPort()
                                + "|";
                        sendType0UDPPacket(msgToSend, portNum, udpChannel);
                        msgsSent++;
                    }
                }
                if (msgsSent == 0) {
                    sendNoConnectionsTCPResponse(cSession);
                    return 1;
                }
                return -1;
            }
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }
    }

    public int handleType0Packet(String msgContents[], DatagramChannel udpChannel) {

        HashMap<Integer, Boolean> dontSendTo = new HashMap<Integer, Boolean>();
        System.out.println("JAVA " + "Port " + getUDPPort() + " is handling a type 0 packet...");

        for (Integer portNum : neighbourUDPPorts.keySet()) {
            System.out.println("JAVA " + "portNum in question: " + portNum);
            dontSendTo.put(portNum, false);
            if (msgContents[4].equalsIgnoreCase(neighbourUDPPorts.get(portNum))) {
                TimetableEntry tmp = findNextDeparture(msgContents[4], Integer.parseInt(msgContents[10]),
                        Integer.parseInt(msgContents[11]));
                if (tmp != null) {
                    sendType3UDPPacket(udpChannel, Integer.parseInt(msgContents[3]), Integer.parseInt(msgContents[5]),
                            Integer.parseInt(msgContents[6]), Integer.parseInt(msgContents[7]), msgContents[8],
                            msgContents[9], tmp.getArriveHour(), tmp.getArriveMin(), msgContents[4]);
                    return 1;
                } else {
                    System.out.println("JAVA " + "Port " + getUDPPort() + " is not sending a packet to " + portNum);
                    dontSendTo.put(portNum, true);
                }
            }
        }

        boolean avail = false;

        System.out.println();

        String[] nodesVisited = msgContents[12].split("[|]");

        for (Integer portNum : neighbourUDPPorts.keySet()) {
            boolean contains = Arrays.stream(nodesVisited).anyMatch(Integer.toString(portNum)::equals);
            if (contains) {
                dontSendTo.put(portNum, true);
            } else {
                avail = true;
            }
        }

        if (!avail) {
            return -20;
        }

        for (Integer port : dontSendTo.keySet()) {
            if (dontSendTo.get(port) == false) {
                String msg = "";
                msg = msg.concat(msgContents[1]).concat(",").concat(Integer.toString(port)).concat(",0,");
                for (int i = 3; i < msgContents.length - 1; i++) {
                    msg = msg.concat(msgContents[i]).concat(",");
                }
                msg = msg.concat(msgContents[12]).concat(msgContents[1]).concat("|");
                sendType0UDPPacket(msg, port, udpChannel);
            }
        }

        return -6;
    }

    public int handleUDPDataRecieved(DatagramChannel udpChannel, SelectionKey key) {
        ByteBuffer byteBuf = ByteBuffer.allocateDirect(2048);
        try {
            InetSocketAddress senderAddress = (InetSocketAddress) udpChannel.receive(byteBuf);
            System.out.println("JAVA " + "Recieved a msg from " + senderAddress.getPort());
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }
        byteBuf.flip();
        String msg = StandardCharsets.UTF_8.decode(byteBuf).toString().replace("\n", "");
        System.out.println("JAVA " + "Port " + getUDPPort() + " has recievied a msg. Msg: " + msg);
        String msgContents[] = msg.split(",");
        for (int i = 0; i < msgContents.length; i++) {
            System.out.println("JAVA " + "msgContents[" + i + "] =" + msgContents[i]);
        }

        switch (Integer.parseInt(msgContents[2])) {

            case 0:
                System.out.println("JAVA " + "UDP Port " + getUDPPort() + " has recieved a type 0 UDP msg");
                return handleType0Packet(msgContents, udpChannel);

            case 1: // The port who sent us the data is asking what our name is
                System.out.println("JAVA " + "UDP Port " + getUDPPort() + " has recieved a type 1 UDP msg");
                sendType2UDPPacket(udpChannel, Integer.parseInt(msgContents[0]));
                return -1;

            case 2: // We recieved a response to our type 1 udp packet and log their name
                System.out.println("JAVA " + "UDP Port " + getUDPPort() + " has recieved a type 2 UDP msg");
                neighbourUDPPorts.put(Integer.parseInt(msgContents[0]), msgContents[3]);
                break;

            case 3: // We recieved a response that contains the final information we need to relay
                    // to the client.
                System.out.println("JAVA " + "UDP Port " + getUDPPort() + " has recieved a type 3 UDP msg");
                if (!clientsFD.containsKey(Integer.parseInt(msgContents[3]))) { // If the client has already recievied a
                                                                                // msg and the socket has been closed
                    System.out.println("JAVA " + "UDP Port " + getUDPPort() + "the client has been closed");
                    return -10;
                }
                ClientSession cSession = clientsFD.get(Integer.parseInt(msgContents[3]));
                TCPResponseStorage responseStorage = cSession.getTCPResponse();
                System.out.println("JAVA " + "responseStorage.getInitTime()=" + responseStorage.getInitTime()
                        + " LocalTime.now().toSecondOfDay()=" + LocalTime.now().toSecondOfDay());
                if (responseStorage.getInitTime() < LocalTime.now().toSecondOfDay()) { // If the timeout period has
                                                                                       // expired
                    return -10;
                }
                if (responseStorage.getFinalHour() > Integer.parseInt(msgContents[8])
                        || (responseStorage.getFinalHour() == Integer.parseInt(msgContents[8])
                                && responseStorage.getFinalMin() > Integer.parseInt(msgContents[9]))) {
                    responseStorage.addPacketRecieved();
                    responseStorage.setInitialTripMethod(msgContents[6]);
                    responseStorage.setInitialTripPlatform(msgContents[7]);
                    responseStorage.setInitialTripHour(Integer.parseInt(msgContents[4]));
                    responseStorage.setInitialTripMin(Integer.parseInt(msgContents[5]));
                    responseStorage.setFinalHour(Integer.parseInt(msgContents[8]));
                    responseStorage.setFinalMin(Integer.parseInt(msgContents[9]));
                    return -11;
                }
                return -12;

            default:
                break;
        }

        return 1;
    }

    public static void main(String[] args) {
        if (args.length < 4) {
            System.out.println("Must have at least 4 arguments.\nUsage: ./station {station_name} "
                    + "{station_TCP_port_number} {station_UDP_port_number} "
                    + "{Connected_station_UDP_port_number}...)");
            System.exit(1);
        }

        station stationServer = new station(args[0], Integer.parseInt(args[1]), Integer.parseInt(args[2]));

        try {
            stationServer.selector = Selector.open();
        } catch (Exception e) {
            e.printStackTrace();
            System.exit(1);
        }

        stationServer.parseTimetableData();
        String fileName = "./tt/tt-";
        fileName = fileName.concat(stationServer.stationName);
        File ttFile = new File(fileName);
        long lastModified = ttFile.lastModified();

        InetSocketAddress tcpSocketAddress = new InetSocketAddress("localhost", stationServer.getTCPPort());
        InetSocketAddress udpSocketAddress = new InetSocketAddress("localhost", stationServer.getUDPPort());

        // -------------------------- Initialize this station's TCP socket channel for
        // listening to incoming connections --------------------------
        ServerSocketChannel tcpSocketChannel = null;
        SelectionKey tcpSelectionKey = null;
        try {
            tcpSocketChannel = ServerSocketChannel.open();
            tcpSocketChannel.configureBlocking(false);
            tcpSelectionKey = tcpSocketChannel.register(stationServer.selector, SelectionKey.OP_ACCEPT);
            tcpSocketChannel.bind(tcpSocketAddress);
        } catch (Exception e) {
            e.printStackTrace();
            System.exit(1);
        }

        // -------------------- Initialize this station's UDP socket
        // -------------------------------------------------------------------
        DatagramChannel udpSocketChannel = null;
        SelectionKey udpSelectionKey = null;
        try {
            udpSocketChannel = DatagramChannel.open();
            udpSocketChannel.configureBlocking(false);
            udpSelectionKey = udpSocketChannel.register(stationServer.selector, SelectionKey.OP_READ);
            udpSocketChannel.bind(udpSocketAddress);
        } catch (Exception e) {
            e.printStackTrace();
            System.exit(1);
        }

        // WAIT so that other servers can initialize before we start pestering them
        System.out.println("JAVA " + "waiting...");
        int waitTime = LocalTime.now().toSecondOfDay() + 2;
        while (LocalTime.now().toSecondOfDay() <= waitTime)
            ;
        System.out.println("JAVA " + "done waiting!");

        // Record all port numbers of neighbouring stations and send out udp packets
        // asking for them to send their station name back
        for (int i = 3; i < args.length; i++) {
            stationServer.neighbourUDPPorts.put(Integer.parseInt(args[i]), "Placeholder");
            stationServer.sendType1UDPPacket(udpSocketChannel, Integer.parseInt(args[i]));
        }

        int clientSocketFDCounter = -1;

        while (true) {
            if (stationServer.clients.size() > 0) {
                for (SelectionKey key : stationServer.clients.keySet()) {
                    TCPResponseStorage tmp = stationServer.clients.get(key).getTCPResponse();
                    if (tmp.getIsUsed() && LocalTime.now().toSecondOfDay() >= tmp.getInitTime()) {
                        if (tmp.getPacketsRecieved() > 0) {
                            stationServer.sendTCPResponse(stationServer.clients.get(key), tmp.getInitialTripMethod(),
                                    tmp.getInitialTripPlatform(), tmp.getFinalDestination(), tmp.getInitialTripHour(),
                                    tmp.getInitialTripMin(), tmp.getFinalHour(), tmp.getFinalMin());
                        } else {
                            stationServer.sendNoConnectionsTCPResponse(stationServer.clients.get(key));
                        }
                        stationServer.clientsFD.remove(stationServer.clients.get(key).getClientSocketFD());
                        try {
                            stationServer.clients.get(key).getSocketChannel().close();
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                        stationServer.clients.remove(key);
                        key.cancel();
                    }
                }
            }

            if (lastModified < ttFile.lastModified()) {
                stationServer.parseTimetableData();
                lastModified = ttFile.lastModified();
            }

            try {
                stationServer.selector.selectNow();
            } catch (IOException e) {
                e.printStackTrace();
                continue;
            }

            for (SelectionKey key : stationServer.selector.selectedKeys()) {
                try {
                    if (!key.isValid()) { // If the key is not valid ignore it and keep going
                        continue;
                    }

                    if (key == tcpSelectionKey) { // If the TCP socket is ready to accept a new connection
                        System.out.println("JAVA " + stationServer.getStationName() + " accepting connection..");
                        SocketChannel acceptedChannel = tcpSocketChannel.accept();
                        acceptedChannel.configureBlocking(false);
                        SelectionKey readKey = acceptedChannel.register(stationServer.selector, SelectionKey.OP_READ);
                        clientSocketFDCounter++;
                        stationServer.clients.put(readKey,
                                new ClientSession(readKey, acceptedChannel, clientSocketFDCounter));
                        stationServer.clientsFD.put(clientSocketFDCounter, stationServer.clients.get(readKey));
                        System.out.println("JAVA " + stationServer.getStationName() + " accepted connection");
                    } else if (key == udpSelectionKey) {
                        System.out.println("JAVA " + stationServer.getStationName() + " udp port "
                                + stationServer.getUDPPort() + " is trying to handle UDP connection...");
                        stationServer.handleUDPDataRecieved(udpSocketChannel, key);
                        System.out.println("JAVA " + stationServer.getStationName() + " udp port "
                                + stationServer.getUDPPort() + " finished handling UDP connection");
                        // Handle the UDP connection
                    } else { // The client connection has sent data and we need to handle that shit
                        System.out.println("JAVA " + stationServer.getStationName()
                                + " is trying to handle a client connection...");
                        ClientSession curSesh = stationServer.clients.get(key);
                        if (stationServer.handleTCPConnection(curSesh, udpSocketChannel) == 1) {
                            stationServer.clientsFD.remove(stationServer.clients.get(key).getClientSocketFD());
                            stationServer.clients.remove(key);
                            key.cancel();
                            curSesh.getSocketChannel().close();
                            System.out.println(
                                    "JAVA " + stationServer.getStationName() + " client connection handled...");
                        } // Else we wait for a UDP to be sent to us that has the result we need to send
                          // over the client socket via TCP
                    }
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }

            stationServer.selector.selectedKeys().clear(); // Reset the keys for the next loop
        }

    }
}