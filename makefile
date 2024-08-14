TARGET = usherette
COMP = g++

# Use  make r=1  for release version
ifdef r
BUG =
else
BUG = -ggdb -DBUG=YES
endif

CFLAGS = `pkg-config --cflags gtk+-3.0` -I../plib  $(BUG) -fPIC 
LFLAG = -L../plib `pkg-config --libs gtk+-3.0` -rdynamic -lplib -lcurl -ljson-c -licuuc -licudata -licui18n -llz4

$(TARGET): $(TARGET).o $(TARGET).glade
	@$(COMP) -o $@ $<   $(LFLAG)
#	@$(COMP) -o $@ $<   $(LFLAG) -lplib -lcurl -ljson-c  -llz4
	@echo $@ Linked OK

$(TARGET).o: $(TARGET).cpp
	@$(COMP) $(CFLAGS) -c -o $@ $< 
	@echo $@ Compiled OK

clean:
	rm -f *.o *.so *~ main $(TARGET)

