#include <stdio.h>
#include <unistd.h>
#include "mylib.h"

#define NUM 3
#define _1GB (1024*1024*1024)

//check metadata is maintained properly and allocation happens from correct location

int main()
{
	// char *p_ = 0;
	// char *q_ = 0;
	// unsigned long size = 0;
	
	// p_ = (char *)memalloc(1);
	// if(p_ == NULL)
	// {
	// 	printf("1.Testcase failed\n");
	// 	return -1;
	// }

	// q_ = (char *)memalloc(9);
	// if(q_ == NULL)
	// {
	// 	printf("2.Testcase failed\n");
	// 	return -1;
	// }

	// if(q_ != p_+16)
	// {
	// 	printf("3.Testcase failed\n");
	// 	return -1;
	// }


	// size = *((unsigned long*)q_ - 1);
	// if(size != 24)
	// {
	// 	printf("4.Testcase failed\n");
	// 	return -1;
	// }


	// printf("Testcase passed\n");

	// char *p1 = 0;
	// char *p2 = 0;
	// char *p3 = 0;

	// p1 = (char *)memalloc(16);
	// if((p1 == NULL) || (p1 == (void*)-1))
	// {
	// 	printf("1.Testcase failed\n");
	// 	return -1;
	// }

	// p2 = (char *)memalloc(16);
	// if((p2 == NULL) || (p2 == (void*)-1))
	// {
	// 	printf("2.Testcase failed\n");
	// 	return -1;
	// }

	// p3 = (char *)memalloc(16);
	// if((p3 == NULL) || (p3 == (void*)-1))
	// {
	// 	printf("3.Testcase failed\n");
	// 	return -1;
	// }
	// printf("Testcase passed\n");

	char *p[NUM];
	char *q = 0;

	for(int i = 0; i < NUM; i++)
	{
		p[i] = (char*)memalloc(_1GB);
		if((p[i] == NULL) || (p[i] == (char*)-1))
		{
			printf("1.Testcase failed\n");
			return -1;
		}

		for(int j = 0; j < _1GB; j++)
		{
			p[i][j] = 'a';
		}
	}

	printf("Testcase passed\n");
	return 0;
}
