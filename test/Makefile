CFLAGS = "-I../Transmission -I../Reception"
LIBS = "-ltransmission -lreception"
LDFLAGS = "-L../Transmission -L../Reception"

sender: sender.c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) $(LIBS)
