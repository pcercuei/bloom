#include <kos.h>

#include <stdio.h>
#include <stdlib.h>

/* I use this program to test small utility functions. I test
*  different variations of the same function to find which is 
*  best/fastest.
*/

int test0() {
    return 0;
}

int test1() {
	return 1;
}

int test2() {
	return 2;
}

int main(int argc, char **argv) {

    vid_set_mode(DM_640x480, PM_RGB555);
    vid_clear(255, 0, 255);
    int i;

    for(i=0;i<1000;i++) {
        test0();
        test1();
        test2();
    }

    return 0;
}