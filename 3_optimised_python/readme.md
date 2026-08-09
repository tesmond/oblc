# Optimised Python

The file we are parsing is just under 14 GB in size. The computer I am processing this on can process memory at 400 GB/s so once this file is in memory the minimum time to read it is 0.035s. However, it is difficult to breach a 45 GB/s level when processing data through the CPU leading to a more plausible maximum of 0.3 seconds.

The first major optimisation is to split up the processing of the file into chunks to allow multiple processes to pass the file and perform the calculations. What we are doing in the code is calculating the number of CPU's and splitting the file into chunks of CPU counts. Then what we are doing is taking each of these chunks and calculating where the line character is so that we can create an appropriately sized batch.

Each batch is similarly processed line by line building up a dict of cities. In order to help with the optimisation, we are actually processing this file in bytes. Additional optimisation at this stage is to process the floats using byte code and some magic numbers as this is faster than passing the strings into floats and performing floating point operations.

Once each of the batches is processed in parallel, then they are brought back together and reduced into a single dictionary with the correct values calculated and then printed to the Console.

At the end of this process, we have a 33 second processing time. Of course 33 seconds is well away from our ideal 0.3 seconds processing time so is there anyway we can improve this?