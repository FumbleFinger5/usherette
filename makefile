TARGET = usherette
COMP = g++

CFLAGS = -I../plib  -isystem /usr/include/x86_64-linux-gnu/qt5 \
   -isystem /usr/include/x86_64-linux-gnu/qt5/QtCore \
   -isystem /usr/include/x86_64-linux-gnu/qt5/QStandardItemModel \
   -isystem /usr/include/x86_64-linux-gnu/qt5/QSettings \
   -isystem  /usr/include/x86_64-linux-gnu/qt5/QtWidgets -ggdb  -fPIC 

LFLAG = -L../plib -L/usr/lib/qt5/bin  -L/usr/local/lib  -L/lib/x86_64-linux-gnu \
 -L/usr/lib/x86_64-linux-gnu/libQt5Widgets.so -L/usr/lib/x86_64-linux-gnu/libQt5Core.so

$(TARGET): $(TARGET).o
	$(COMP) $(CFLAGS)  $(LFLAG) -L/usr/local/lib -o $@ $<    -lplib -lQt5Widgets -lQt5Core
	@cp $(TARGET) ~/Documents/latest
#	$(COMP) $(CFLAGS)  $(LFLAG) -L/usr/local/lib -o $@ $<    -lplib

$(TARGET).o: $(TARGET).cpp
	$(COMP) $(CFLAGS)  $(LFLAG)  -c -o $@ $< 

clean:
	rm -f *.o *.so main $(TARGET)

