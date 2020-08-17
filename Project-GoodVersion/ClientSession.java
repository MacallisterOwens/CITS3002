import java.io.IOException;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;

public class ClientSession {
    private SelectionKey sKey;
    private int clientSocketFD;
    private SocketChannel sChannel;
    private TCPResponseStorage tcpResponse;

    public ClientSession(SelectionKey sKey, SocketChannel sChannel, int  clientSocketFD) {
        try {
            System.out.println("making a new client session");
            this.sKey = sKey;
            this.sChannel = (SocketChannel)sChannel.configureBlocking(false);
            tcpResponse = new TCPResponseStorage();
            this.clientSocketFD = clientSocketFD;
        } catch(IOException e) {
            e.printStackTrace();
        }
    }

    public int getClientSocketFD() {
        return clientSocketFD;
    }

    public SelectionKey getSelectionKey() {
        return sKey;
    }

    public SocketChannel getSocketChannel() {
        return sChannel;
    }

    public TCPResponseStorage getTCPResponse() {
        return tcpResponse;
    }
}