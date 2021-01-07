include ../config.mak

NAME = libreception
LIBNAME = $(NAME).so.$(MAJOR).$(MINOR)
SONAME = $(NAME).so.$(MAJOR)
STATICLIB = $(NAME).a

all: $(LIBNAME) $(STATICLIB)

$(LIBNAME): foo.o bar.o
	$(CC) -shared -o $@ $^ -Wl,-soname,$(SONAME) -fPIC

$(STATICLIB): foo.o bar.o
	$(AR) cq $@ $^

.c.o:
	$(CC) -c $<
