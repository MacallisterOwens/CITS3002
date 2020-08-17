
public class TimetableEntry {

    private int departHour;
    private int departMin;
    private int arriveHour;
    private int arriveMin;
    private String lineOrBus;
    private String departurePlatform;
    private String destStation;

    public TimetableEntry() {
        departHour = 30;
        departMin = 70;
        arriveHour = 30;
        arriveMin = 70;
        lineOrBus = "placeholder";
        departurePlatform = "placeholder";
        destStation = "placeholder";
    }

    public void setDepartHour(int hour) {
        departHour = hour;
    }

    public void setDepartMin(int min) {
        departMin = min;
    }

    public void setArriveHour(int hour) {
        arriveHour = hour;
    }

    public void setArriveMin(int min) {
        arriveMin = min;
    }

    public void setLineOrBus(String lineOrBus) {
        this.lineOrBus = lineOrBus;
    }

    public void setDeparturePlatform(String platform) {
        departurePlatform = platform;
    }

    public void setDestStation(String dest) {
        destStation = dest;
    }

    public int getDepartHour() {
        return departHour;
    }

    public int getDepartMin() {
        return departMin;
    }

    public int getArriveHour() {
        return arriveHour;
    }

    public int getArriveMin() {
        return arriveMin;
    }

    public String getLineOrBus() {
        return lineOrBus;
    }

    public String getDeparturePlatform() {
        return departurePlatform;
    }

    public String getDestStation() {
        return destStation;
    }
}