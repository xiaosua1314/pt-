
#define WINDOWSIZE 20   // Integrator window size, in samples. The article recommends 150ms. So, FS*0.15.
						// However, you should check empirically if the waveform looks ok.
#define NOSAMPLE -32000 // An indicator that there are no more samples to read. Use an impossible value for a sample.
#define FS 360          // Sampling frequency.
#define BUFFSIZE 600    // The size of the buffers (in samples). Must fit more than 1.66 times an RR interval, which
                        // typically could be around 1 second.

#define DELAY 22		// Delay introduced by the filters. Filter only output samples after this one.
						// Set to 0 if you want to keep the delay. Fixing the delay results in DELAY less samples
						// in the final end result.

#include "panTompkins.h"
#include <stdio.h>      // Remove if not using the standard file functions.


FILE *fin, *fout;       // Remove them if not using files and <stdio.h>.

/*
    Use this function for any kind of setup you need before getting samples.
    This is a good place to open a file, initialize your hardware and/or open
    a serial connection.
    Remember to update its parameters on the panTompkins.h file as well.
*/
void init(char file_in[], char file_out[])
{
	fin = fopen(file_in, "r");
	fout = fopen(file_out, "w");
}

/*
    Use this function to read and return the next sample (from file, serial,
    A/D converter etc) and put it in a suitable, numeric format. Return the
    sample, or NOSAMPLE if there are no more samples.
*/
dataType input()
{
	int num = NOSAMPLE;
	if (!feof(fin))
		fscanf(fin, "%d", &num);

	return num;
}

/*
    Use this function to output the information you see fit (last RR-interval,
    sample index which triggered a peak detection, whether each sample was a R
    peak (1) or not (0) etc), in whatever way you see fit (write on screen, write
    on file, blink a LED, call other functions to do other kinds of processing,
    such as feature extraction etc). Change its parameters to receive the necessary
    information to output.
*/
void output(int out)
{
	fprintf(fout, "%d\n", out);
}

/*
    This is the actual QRS-detecting function. It's a loop that constantly calls the input and output functions
    and updates the thresholds and averages until there are no more samples. More details both above and in
    shorter comments below.
*/
void panTompkins()
{
    // The signal array is where the most recent samples are kept. The other arrays are the outputs of each
    // filtering module: DC Block, low pass, high pass, integral etc.
	// The output is a buffer where we can change a previous result (using a back search) before outputting.
	dataType signal[BUFFSIZE], dcblock[BUFFSIZE], lowpass[BUFFSIZE], highpass[BUFFSIZE], derivative[BUFFSIZE], squared[BUFFSIZE], integral[BUFFSIZE], outputSignal[BUFFSIZE];

	// rr1 holds the last 8 RR intervals. rr2 holds the last 8 RR intervals between rrlow and rrhigh.
	// rravg1 is the rr1 average, rr2 is the rravg2. rrlow = 0.92*rravg2, rrhigh = 1.08*rravg2 and rrmiss = 1.16*rravg2.
	// rrlow is the lowest RR-interval considered normal for the current heart beat, while rrhigh is the highest.
	// rrmiss is the longest that it would be expected until a new QRS is detected. If none is detected for such
	// a long interval, the thresholds must be adjusted.
	int rr1[8], rr2[8], rravg1, rravg2, rrlow = 0, rrhigh = 0, rrmiss = 0;

	// i and j are iterators for loops.
	// sample counts how many samples have been read so far.
	// lastQRS stores which was the last sample read when the last R sample was triggered.
	// lastSlope stores the value of the squared slope when the last R sample was triggered.
	// currentSlope helps calculate the max. square slope for the present sample.
	// These are all long unsigned int so that very long signals can be read without messing the count.
	long unsigned int i, j, sample = 0, lastQRS = 0, lastSlope = 0, currentSlope = 0;

	// This variable is used as an index to work with the signal buffers. If the buffers still aren't
	// completely filled, it shows the last filled position. Once the buffers are full, it'll always
	// show the last position, and new samples will make the buffers shift, discarding the oldest
	// sample and storing the newest one on the last position.
	int current;

	// There are the variables from the original Pan-Tompkins algorithm.
	// The ones ending in _i correspond to values from the integrator.
	// The ones ending in _f correspond to values from the DC-block/low-pass/high-pass filtered signal.
	// The peak variables are peak candidates: signal values above the thresholds.
	// The threshold 1 variables are the threshold variables. If a signal sample is higher than this threshold, it's a peak.
	// The threshold 2 variables are half the threshold 1 ones. They're used for a back search when no peak is detected for too long.
	// The spk and npk variables are, respectively, running estimates of signal and noise peaks.
	dataType peak_i = 0, peak_f = 0, threshold_i1 = 0, threshold_i2 = 0, threshold_f1 = 0, threshold_f2 = 0, spk_i = 0, spk_f = 0, npk_i = 0, npk_f = 0;

	// qrs tells whether there was a detection or not.
	// regular tells whether the heart pace is regular or not.
	// prevRegular tells whether the heart beat was regular before the newest RR-interval was calculated.
	bool qrs, regular = true, prevRegular;

	// Initializing the RR averages
	for (i = 0; i < 8; i++)
    {
        rr1[i] = 0;
        rr2[i] = 0;
    }

    // The main loop where everything proposed in the paper happens. Ends when there are no more signal samples.
    do{
        // Test if the buffers are full.
        // If they are, shift them, discarding the oldest sample and adding the new one at the end.
        // Else, just put the newest sample in the next free position.
        // Update 'current' so that the program knows where's the newest sample.
		if (sample >= BUFFSIZE)
		{
			for (i = 0; i < BUFFSIZE - 1; i++)
			{
				signal[i] = signal[i+1];
				dcblock[i] = dcblock[i+1];
				lowpass[i] = lowpass[i+1];
				highpass[i] = highpass[i+1];
				derivative[i] = derivative[i+1];
				squared[i] = squared[i+1];
				integral[i] = integral[i+1];
				outputSignal[i] = outputSignal[i+1];
			}
			current = BUFFSIZE - 1;
		}
		else
		{
			current = sample;
		}
		signal[current] = input();

		// If no sample was read, stop processing!
		if (signal[current] == NOSAMPLE)
			break;
		sample++; // Update sample counter

		// DC Block filter
		// This was not proposed on the original paper.
		// It is not necessary and can be removed if your sensor or database has no DC noise.
		if (current >= 1)
			dcblock[current] = signal[current] - signal[current-1] + 0.995*dcblock[current-1];
		else
			dcblock[current] = 0;

		// Low Pass filter
		// Implemented as proposed by the original paper.
		// y(nT) = 2y(nT - T) - y(nT - 2T) + x(nT) - 2x(nT - 6T) + x(nT - 12T)
		// Can be removed if your signal was previously filtered, or replaced by a different filter.
		lowpass[current] = dcblock[current];
		if (current >= 1)
			lowpass[current] += 2*lowpass[current-1];
		if (current >= 2)
			lowpass[current] -= lowpass[current-2];
		if (current >= 6)
			lowpass[current] -= 2*dcblock[current-6];
		if (current >= 12)
			lowpass[current] += dcblock[current-12];

		// High Pass filter
		// Implemented as proposed by the original paper.
		// y(nT) = 32x(nT - 16T) - [y(nT - T) + x(nT) - x(nT - 32T)]
		// Can be removed if your signal was previously filtered, or replaced by a different filter.
		highpass[current] = -lowpass[current];
		if (current >= 1)
			highpass[current] -= highpass[current-1];
		if (current >= 16)
			highpass[current] += 32*lowpass[current-16];
		if (current >= 32)
			highpass[current] += lowpass[current-32];


		// y(nT) = (1/8T)[-x(nT - 2T) - 2x(nT - T) + 2x(nT + T) + x(nT + 2T)]
        derivative[current] = highpass[current];
		if (current > 0)
			derivative[current] -= highpass[current-1];

		squared[current] = derivative[current]*derivative[current];


		integral[current] = 0;
		for (i = 0; i < WINDOWSIZE; i++)
		{
			if (current >= (dataType)i)
				integral[current] += squared[current - i];
			else
				break;
		}
		integral[current] /= (dataType)i;

		qrs = false;

        if (integral[current] >= threshold_i1 || highpass[current] >= threshold_f1)
        {
            peak_i = integral[current];
            peak_f = highpass[current];
        }

		if ((integral[current] >= threshold_i1) && (highpass[current] >= threshold_f1))
		{

			if (sample > lastQRS + FS/5)
			{

				if (sample <= lastQRS + (long unsigned int)(0.36*FS))
				{

				    currentSlope = 0;
				    for (j = current - 10; j <= current; j++)
                        if (squared[j] > currentSlope)
                            currentSlope = squared[j];

				    if (currentSlope <= (dataType)(lastSlope/2))
                    {
                        qrs = false;
                    }

                    else
                    {
                        spk_i = 0.125*peak_i + 0.875*spk_i;
                        threshold_i1 = npk_i + 0.25*(spk_i - npk_i);
                        threshold_i2 = 0.5*threshold_i1;

                        spk_f = 0.125*peak_f + 0.875*spk_f;
                        threshold_f1 = npk_f + 0.25*(spk_f - npk_f);
                        threshold_f2 = 0.5*threshold_f1;

                        lastSlope = currentSlope;
                        qrs = true;
                    }
				}

				else
				{
				    currentSlope = 0;
                    for (j = current - 10; j <= current; j++)
                        if (squared[j] > currentSlope)
                            currentSlope = squared[j];

                    spk_i = 0.125*peak_i + 0.875*spk_i;
                    threshold_i1 = npk_i + 0.25*(spk_i - npk_i);
                    threshold_i2 = 0.5*threshold_i1;

                    spk_f = 0.125*peak_f + 0.875*spk_f;
                    threshold_f1 = npk_f + 0.25*(spk_f - npk_f);
                    threshold_f2 = 0.5*threshold_f1;

                    lastSlope = currentSlope;
                    qrs = true;
				}
			}

			else
            {
                peak_i = integral[current];
				npk_i = 0.125*peak_i + 0.875*npk_i;
				threshold_i1 = npk_i + 0.25*(spk_i - npk_i);
				threshold_i2 = 0.5*threshold_i1;
				peak_f = highpass[current];
				npk_f = 0.125*peak_f + 0.875*npk_f;
				threshold_f1 = npk_f + 0.25*(spk_f - npk_f);
                threshold_f2 = 0.5*threshold_f1;
                qrs = false;
				outputSignal[current] = qrs;
				if (sample > DELAY + BUFFSIZE)
                	output(outputSignal[0]);
                continue;
            }

		}

		if (qrs)
		{

			rravg1 = 0;
			for (i = 0; i < 7; i++)
			{
				rr1[i] = rr1[i+1];
				rravg1 += rr1[i];
			}
			rr1[7] = sample - lastQRS;
			lastQRS = sample;
			rravg1 += rr1[7];
			rravg1 *= 0.125;

			if ( (rr1[7] >= rrlow) && (rr1[7] <= rrhigh) )
			{
				rravg2 = 0;
				for (i = 0; i < 7; i++)
				{
					rr2[i] = rr2[i+1];
					rravg2 += rr2[i];
				}
				rr2[7] = rr1[7];
				rravg2 += rr2[7];
				rravg2 *= 0.125;
				rrlow = 0.92*rravg2;
				rrhigh = 1.16*rravg2;
				rrmiss = 1.66*rravg2;
			}

			prevRegular = regular;
			if (rravg1 == rravg2)
			{
				regular = true;
			}

			else
			{
				regular = false;
				if (prevRegular)
				{
					threshold_i1 /= 2;
					threshold_f1 /= 2;
				}
			}
		}

		else
		{

			if ((sample - lastQRS > (long unsigned int)rrmiss) && (sample > lastQRS + FS/5))
			{
				for (i = current - (sample - lastQRS) + FS/5; i < (long unsigned int)current; i++)
				{
					if ( (integral[i] > threshold_i2) && (highpass[i] > threshold_f2))
					{
					    currentSlope = 0;
                        for (j = i - 10; j <= i; j++)
                            if (squared[j] > currentSlope)
                                currentSlope = squared[j];

                        if ((currentSlope < (dataType)(lastSlope/2)) && (i + sample) < lastQRS + 0.36*lastQRS)
                        {
                            qrs = false;
                        }
                        else
                        {
                            peak_i = integral[i];
                            peak_f = highpass[i];
                            spk_i = 0.25*peak_i+ 0.75*spk_i;
                            spk_f = 0.25*peak_f + 0.75*spk_f;
                            threshold_i1 = npk_i + 0.25*(spk_i - npk_i);
                            threshold_i2 = 0.5*threshold_i1;
                            lastSlope = currentSlope;
                            threshold_f1 = npk_f + 0.25*(spk_f - npk_f);
                            threshold_f2 = 0.5*threshold_f1;

                            rravg1 = 0;
                            for (j = 0; j < 7; j++)
                            {
                                rr1[j] = rr1[j+1];
                                rravg1 += rr1[j];
                            }
                            rr1[7] = sample - (current - i) - lastQRS;
                            qrs = true;
                            lastQRS = sample - (current - i);
                            rravg1 += rr1[7];
                            rravg1 *= 0.125;

                            if ( (rr1[7] >= rrlow) && (rr1[7] <= rrhigh) )
                            {
                                rravg2 = 0;
                                for (i = 0; i < 7; i++)
                                {
                                    rr2[i] = rr2[i+1];
                                    rravg2 += rr2[i];
                                }
                                rr2[7] = rr1[7];
                                rravg2 += rr2[7];
                                rravg2 *= 0.125;
                                rrlow = 0.92*rravg2;
                                rrhigh = 1.16*rravg2;
                                rrmiss = 1.66*rravg2;
                            }

                            prevRegular = regular;
                            if (rravg1 == rravg2)
                            {
                                regular = true;
                            }
                            else
                            {
                                regular = false;
                                if (prevRegular)
                                {
                                    threshold_i1 /= 2;
                                    threshold_f1 /= 2;
                                }
                            }

                            break;
                        }
                    }
				}

				if (qrs)
                {
                    outputSignal[current] = false;
                    outputSignal[i] = true;
                    if (sample > DELAY + BUFFSIZE)
                        output(outputSignal[0]);
                    continue;
                }
			}


			if (!qrs)
			{				

				if ((integral[current] >= threshold_i1) || (highpass[current] >= threshold_f1))
				{
					peak_i = integral[current];
					npk_i = 0.125*peak_i + 0.875*npk_i;
					threshold_i1 = npk_i + 0.25*(spk_i - npk_i);
					threshold_i2 = 0.5*threshold_i1;
					peak_f = highpass[current];
					npk_f = 0.125*peak_f + 0.875*npk_f;
					threshold_f1 = npk_f + 0.25*(spk_f - npk_f);
					threshold_f2 = 0.5*threshold_f1;
				}
			}
		}

		outputSignal[current] = qrs;
		if (sample > DELAY + BUFFSIZE)
			output(outputSignal[0]);
	} while (signal[current] != NOSAMPLE);

	for (i = 1; i < BUFFSIZE; i++)
		output(outputSignal[i]);

	fclose(fin);
	fclose(fout);
}
int main(){
	char * input_file="/home/xiao/QRSCCCCCc/examples/test_input.txt";
	char * output_file="/home/xiao/QRStest.txt";
	init(input_file, output_file);
	void panTompkins();
}