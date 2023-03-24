a3w23: a3w23.o
	g++ -o a3w23 a3w23.o

a3w23.o: a3w23.cc
	g++ -c a3w23.cc -o a3w23.o

make clean:
	rm *.o

tar:
	tar -cvf Hossain-a3.tar a3w23.cc a3-ex1.dat Assignment3Report.pdf makefile