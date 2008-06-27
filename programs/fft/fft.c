/********************************************
 * FFT, C version
 *
 * Also see http://www.dspguide.com/ch12.htm
 ********************************************/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
 
#define PI 3.1415926535897932384626433832795

typedef struct
{
	double re, im;
} complex;

static void FFT(complex* X, int M)
{
	int i, j, k;
	int N = (1 << M);

	for (k = M; k > 0; k--)
	{
		int LE    = 1 << k;
		int LE2   = LE / 2;
		complex S = {cos(PI/LE2), -sin(PI/LE2)};
		complex U = {1.0, 0.0};
		
		for (j = 0; j < LE2; j++)
		{
			double re;
			
			for (i = j; i < N; i += LE)
			{
				int ip = i + LE2;
				
				/* complex T =  X[i] + X[ip]; */
				complex T = {
					X[i].re + X[ip].re,
					X[i].im + X[ip].im
				};

				/* X[ip] = (X[i] - X[ip]) * U; */
				re = X[ip].re;
				X[ip].re = (X[i].re - X[ip].re) * U.re - (X[i].im - X[ip].im) * U.im;
				X[ip].im = (X[i].im - X[ip].im) * U.re + (X[i].re -       re) * U.im;

				X[i] = T;
			}
			
			/* U = U * S; */
			re = U.re;
			U.re = U.re * S.re - U.im * S.im;
			U.im = U.im * S.re +   re * S.im;
		}
	}

	j = 0;
	for (i = 0; i < N - 1; i++)
	{
		if (i < j)
		{
			/* Swap X[i] and X[j] */
			complex T = X[j];
			X[j] = X[i];
			X[i] = T;
		}

		k = N/2;
		while (k - 1 < j)
		{
			j = j - k;
			k = k / 2;
		}
		j = j + k;
	}
}

static void InvFFT(complex* X, int M)
{
	int N = 1 << M;
	int i;
	
	for (i = 0; i < N; i++)
	{
		X[i].im = -X[i].im;
	}
	
	FFT(X, M);
	
	for (i = 0; i < N; i++)
	{
		X[i].re =  X[i].re / N;
		X[i].im = -X[i].im / N;
	}
}

int main()
{
	int M = 8;
	int N = 1 << M;
	int i;
	
	/* Test of the FFT algorithm
	 * If the FFT is working correctly, the following should hold:
	 * - Applying the FFT followed by the Inverse FFT should produce the
	 *   input again.
	 * - If the imaginary part of the input is 0, apply FFT and check the
	 *   result. The following should hold:
	 *   + Except for the first element, the result should be symmetrical
	 *     around the center element (which should have im = 0).
	 *   + The imaginary part should be sign-flipped symmetrical.
	 *   + The first element's imaginary number should be zero.
	 */
	complex* X = malloc(N * sizeof(complex));
	complex* Y = malloc(N * sizeof(complex));
	complex* Z = malloc(N * sizeof(complex));
	
	srand(time(NULL));
	for (i = 0; i < N; i++)
	{
		X[i].re = 0.0f;
		X[i].im = 0.0f;
		Y[i]    = X[i];
	}

	for (i = 0; i < (1 << 4); i++)
	{
		X[i].re = (double)i + 1;
		X[i].im = 0.0f;
		Y[i]    = X[i];
	}
	
	FFT(Y, M);
	memcpy(Z, Y, N * sizeof(complex));
	InvFFT(Z, M);
	
	for (i = 0; i < N; i++)
	{
		printf("%8.2lf %8.2lf   -   %8.2lf %8.2lf   -   %8.2lf %8.2lf\n", X[i].re, X[i].im, Y[i].re, Y[i].im, Z[i].re, Z[i].im );
	}

	for (i = 0; i < N; i++)
	{
		printf("%016llx %016llx\n", *(unsigned long long*)&Y[i].re, *(unsigned long long*)&Y[i].im);
	}
	
	free(X);
	free(Y);
	free(Z);
	
	return 0;
}
