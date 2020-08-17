public class TCPResponseStorage {
    private int initTime;
    private boolean isUsed;
    private int packetsRecieved;
    private String initialTripMethod;
    private String initialTripPlatform;
    private String finalDestination;
    private int initialTripHour;
    private int initialTripMin;
    private int finalHour;
    private int finalMin;
    
    public TCPResponseStorage() {
        packetsRecieved = 0;
        isUsed = false;
        initialTripMethod = "placeholder";
        initialTripPlatform = "placeholder";
        finalDestination = "placeholder";
        initialTripHour = 30;
        initialTripMin = 70;
        finalHour = 30;
        finalMin = 70;
    }

    public void addPacketRecieved() {
        packetsRecieved++;
    }

    public void setInitTime(int initTime) {
        this.initTime = initTime;
    }

    public void setIsUsed(boolean isUsed) {
        this.isUsed = isUsed;
    }

    public void resetPacketsRecieved() {
        packetsRecieved = 0;
    }

    public void setInitialTripMethod(String initialMethod) {
        initialTripMethod = initialMethod;
    }
    
    public void setInitialTripPlatform(String initialPlatform) {
        initialTripPlatform = initialPlatform;
    }

    public void setFinalDestination(String finalDest) {
        finalDestination = finalDest;
    }

    public void setInitialTripHour(int hour) {
        initialTripHour = hour;
    }

    public void setInitialTripMin(int min) {
        initialTripMin = min;
    }

    public void setFinalHour(int hour) {
        finalHour = hour;
    }

    public void setFinalMin(int min) {
        finalMin = min;
    }

    public int getInitTime() {
        return initTime;
    }

    public boolean getIsUsed() {
        return isUsed;
    }

    public int getPacketsRecieved() {
        return packetsRecieved;
    }

    public String getInitialTripMethod() {
        return initialTripMethod;
    }

    public String getInitialTripPlatform() {
        return initialTripPlatform;
    }

    public String getFinalDestination() {
        return finalDestination;
    }

    public int getInitialTripHour() {
        return initialTripHour;
    }

    public int getInitialTripMin() {
        return initialTripMin;
    }

    public int getFinalHour() {
        return finalHour;
    }

    public int getFinalMin() {
        return finalMin;
    }
}