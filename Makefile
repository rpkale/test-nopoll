test_nopoll: test_nopoll.cpp
	gcc -Wall -I ${HOME}/libnopoll/include/nopoll test_nopoll.cpp -o test_nopoll -L ${HOME}/libnopoll/lib -lnopoll
clean:
	rm test_nopoll
