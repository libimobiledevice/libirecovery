all: static
	@echo "Please choose either macosx, linux, or windows"

static:
	gcc -o libirecovery.o -c src/libirecovery.c -g -I./include
	ar rs libirecovery.a libirecovery.o
	gcc -o irecovery src/irecovery.c -g -I./include -L. -lirecovery -lreadline -lusb-1.0

linux:
	gcc -o libirecovery.o -c src/libirecovery.c -g -I./include -lreadline -fPIC 
	gcc -o libirecovery.so libirecovery.o -g -shared -Wl,-soname,libirecovery.so -lusb-1.0
	gcc -o irecovery src/irecovery.c -g -I./include -L. -lirecovery

macosx:
	gcc -o libirecovery.dylib -c src/libirecovery.c -dynamiclib
	gcc -o irecovery irecovery.c -I. -lirecovery
	
windows:
	gcc -o libirecovery.dll -c src/libirecovery.c -I. -lusb-1.0 -lreadline -shared -fPIC
	gcc -o irecovery irecovery.c -I. -lirecovery
		
clean:
	rm -rf irecovery libirecovery.o libirecovery.so
		

		
