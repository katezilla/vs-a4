# uebersetzen und starten des sniffers:
make
./sniffer <portnummer>


# uebersetzen und starten des gesamten Projekts:
make all
./sniffer <portnummer>
./clocksync <hostname> <portnummer> <adresse> <slotnummer>
233.0.0.1

# visualisieren des sniffer ergebnisses
java -jar ClocksyncVisualizer.jar <Pfad zur Capture-Datei>