#include <iostream>
#include <vector>

#include "Utils.h"
#include "CImg.h"

using namespace cimg_library;
using namespace std;

// Returns console information about different flags that can be passed to the function
void print_help() {
	std::cerr << "Application usage:" << std::endl;

	std::cerr << "  -p : select platform " << std::endl;
	std::cerr << "  -d : select device" << std::endl;
	std::cerr << "  -l : list all platforms and devices" << std::endl;
	std::cerr << "  -f : input image file (default: test.ppm)" << std::endl;
	std::cerr << "  -h : print this message" << std::endl;
}

int main(int argc, char **argv) {
	//Part 1 - handle command line options such as device selection, verbosity, etc.
	// Define platform and device ID's as defaults incase no arguements are passed through
	int platform_id = 0;
	int device_id = 0;

	// Load in our initial reference file
	string inputImgFilename = "test.pgm";

	// Handle command line arguements
	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-p") == 0) && (i < (argc - 1))) { platform_id = atoi(argv[++i]); }
		else if ((strcmp(argv[i], "-d") == 0) && (i < (argc - 1))) { device_id = atoi(argv[++i]); }
		else if (strcmp(argv[i], "-l") == 0) { std::cout << ListPlatformsDevices() << std::endl; }
		else if ((strcmp(argv[i], "-f") == 0) && (i < (argc - 1))) { inputImgFilename = argv[++i]; }
		else if (strcmp(argv[i], "-h") == 0) { print_help(); return 0; }
	}

	// Hides CImg library messages/exceptions from the output
	cimg::exception_mode(0);

	//detect any potential exceptions
	try {
		// Returns a pointer to a image location from its filename
		CImg<unsigned char> inputImgPtr(inputImgFilename.c_str());
		CImgDisplay inputImgDisp(inputImgPtr, "Input Image");

		cout << "Width: " << inputImgPtr.width() << ", Height: " << inputImgPtr.height() << endl;
		cout << "Pixel Count (H*W): " << inputImgPtr.height() * inputImgPtr.width() << endl;

		// Select platform and device to use to create a context from
		cl::Context context = GetContext(platform_id, device_id);

		// Display the selected device
		std::cout << "Running on " << GetPlatformName(platform_id) << ", " << GetDeviceName(platform_id, device_id) << std::endl;

		// Create a queue to which we will push commands for the device & enable profiling
		cl::CommandQueue queue(context, CL_QUEUE_PROFILING_ENABLE);
		// Create a CL Event to attatch to queue commands as our profiler

		// Create a program to combine context and kernels
		cl::Program::Sources sources;
		AddSources(sources, "kernels/assign_kernels.cl");

		// Create a program to combine context and kernels
		cl::Program program(context, sources);

		// Attempt to build the OpenCL Program and catch any errors that occur during build
		try { 
			program.build();
		}
		catch (const cl::Error& err) {
			std::cout << "Build Status: " << program.getBuildInfo<CL_PROGRAM_BUILD_STATUS>(context.getInfo<CL_CONTEXT_DEVICES>()[0]) << std::endl;
			std::cout << "Build Options:\t" << program.getBuildInfo<CL_PROGRAM_BUILD_OPTIONS>(context.getInfo<CL_CONTEXT_DEVICES>()[0]) << std::endl;
			std::cout << "Build Log:\t " << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(context.getInfo<CL_CONTEXT_DEVICES>()[0]) << std::endl;
			throw err;
		}

		/* Part 1 - Histogram Generation */

		// Create a host vector (Histogram Bin) to hold our output values
		std::vector<int> histBin(256);
		size_t histBinSize = histBin.size() * sizeof(int);

		// Create our initial buffers for usage in OpenCL Kernels
		cl::Buffer inputImgBuffer(context, CL_MEM_READ_ONLY, inputImgPtr.size()); // Create a read-only buffer with a size of our input image
		cl::Buffer histBuffer(context, CL_MEM_READ_WRITE, histBinSize);  // Create a read-write buffer with the size of our histogram bin (bin_size * size(int))
		
//		cl::Buffer image_output_buffer(context, CL_MEM_READ_ONLY, inputImgPtr.size()); // Not in use yet!
//		cl::Buffer scan_histogram_buffer(context, CL_MEM_READ_WRITE, histBinSize); // Not in use yet!

		////std::cout << "Bin Size: " << histBin.size() << std::endl;
		////std::cout << "Bin Size in Bytes: " << histBinSize << std::endl;

		cl::Event imgBufferEvent;
		cl::Event histBinBufferEvent;

		// Write image input data to our device's memory via our image input buffer
		queue.enqueueWriteBuffer(inputImgBuffer, CL_TRUE, 0, inputImgPtr.size(), &inputImgPtr.data()[0], NULL, &imgBufferEvent);
		// Write histogram bin buffer filled with 0's to our device's memory
		queue.enqueueFillBuffer(histBuffer, 0, 0, histBinSize);

		// Set up histogram kernel for device execution
		cl::Kernel kernelHist = cl::Kernel(program, "histogram"); // Load the histogram kernel defined in my_kernels
		kernelHist.setArg(0, inputImgBuffer);  // Pass in our image buffer as our input
		kernelHist.setArg(1, histBuffer);  // Pass in our histogram buffer as our output

		cl::Device device = context.getInfo<CL_CONTEXT_DEVICES>()[0]; //get device
		std::cout << "Preferred Size:";
		cerr << kernelHist.getWorkGroupInfo<CL_KERNEL_WORK_GROUP_SIZE>(device) << endl; //get info

		cl::Event prof_event;
		queue.enqueueNDRangeKernel(kernelHist, cl::NullRange, cl::NDRange(inputImgPtr.size()), cl::NullRange, NULL, &prof_event);

		// Write the histogram result from our device memory to our vector via the histogram buffer
		queue.enqueueReadBuffer(histBuffer, CL_TRUE, 0, histBinSize, &histBin[0], NULL, &histBinBufferEvent);

		cout << "Image Buffer Memory Write Time [ns]: " << imgBufferEvent.getProfilingInfo<CL_PROFILING_COMMAND_END>() - imgBufferEvent.getProfilingInfo<CL_PROFILING_COMMAND_START>() << endl;
		cout << "Histogram Buffer Memory Write Time [ns]: " << histBinBufferEvent.getProfilingInfo<CL_PROFILING_COMMAND_END>() - histBinBufferEvent.getProfilingInfo<CL_PROFILING_COMMAND_START>() << endl;

		////std::cout << "Original Histogram:\n" << histBin << "\n\n" << std::endl;

		/* Part 2 - Cumulative Histogram Generation */

		// Create a new vector to store our cumulative bin values
		std::vector<int> cumHistBin(histBin.size());
		size_t cumHistBinSize = cumHistBin.size() * sizeof(int);

		// Create a new buffer to hold data about our cumulative histogram on our device
		cl::Buffer cumHistBuffer(context, CL_MEM_READ_WRITE, cumHistBinSize);

		// Write histogram data to our device's memory via our histogram buffer
		queue.enqueueWriteBuffer(histBuffer, CL_TRUE, 0, histBinSize, &histBin[0]);
		// Write cumulative histogram bin buffer filled with 0's to our devices memory
		queue.enqueueFillBuffer(cumHistBuffer, 0, 0, cumHistBinSize);

		////cl_int test_value = 5;

		// Set up cumulative kernel for device execution
		cl::Kernel kernelCumHist = cl::Kernel(program, "scan_add_atomic"); // Load the scan_add_atomic kernel defined in my_kernels
		kernelCumHist.setArg(0, histBuffer); // Pass in our histogram buffer as our input
		kernelCumHist.setArg(1, cumHistBuffer); // Pass in our cumulative histogram buffer as our output
		////kernelCumHist.setArg(2, sizeof(cl_int), &test_value); // Test

		// Execute the cumulative histogram kernel on the selected device
		queue.enqueueNDRangeKernel(kernelCumHist, cl::NullRange, cl::NDRange(histBin.size()), cl::NDRange(256));

		// Copy the result from device to host
		queue.enqueueReadBuffer(cumHistBuffer, CL_TRUE, 0, cumHistBinSize, &cumHistBin[0]);

		////std::cout << "Cumulative Histogram:\n" << cumHistBin << "\n\n" << std::endl;

		/* Part 3 - Cumulative Histogram Normalisation */

		// Create a new vector to store our cumulative bin values
		std::vector<int> normHistBin(cumHistBin.size());
		size_t normHistBinSize = normHistBin.size() * sizeof(int);

		// Create a new buffer to hold data about our normalised cumulative histogram on our device
		cl::Buffer normHistBuffer(context, CL_MEM_READ_WRITE, normHistBinSize);

		// Write histogram data to our device's memory via our cumulative histogram buffer
		queue.enqueueWriteBuffer(cumHistBuffer, CL_TRUE, 0, cumHistBinSize, &cumHistBin[0]);
		// Write normalised cumulative histogram bin buffer filled  with 0's to our devices memory
		queue.enqueueFillBuffer(normHistBuffer, 0, 0, normHistBinSize);

		// Set up normalised cumulative kernel for device execution
		cl::Kernel kernelCumNormHist = cl::Kernel(program, "norm_bins"); // Load the norm_bins kernel defined in my_kernels
		kernelCumNormHist.setArg(0, cumHistBuffer); // Load in the cumulative histogram buffer
		kernelCumNormHist.setArg(1, normHistBuffer); // Pass in our normalised buffer filled with 0's

		// Execute the cumulative histogram kernel on the selected device
		queue.enqueueNDRangeKernel(kernelCumNormHist, cl::NullRange, cl::NDRange(cumHistBin.size()), cl::NDRange(256));

		// Copy the result from device to host
		queue.enqueueReadBuffer(normHistBuffer, CL_TRUE, 0, normHistBinSize, &normHistBin[0]);

		////cout << "\nNormalised Cumalitive Histogram:\n" << normHistBin << "\n\n" << endl;

		/* Part 4 - Image from LUT */

		// Create an output buffer to store values copied from device once computation is complete
		vector<unsigned char> outputImgVect(inputImgPtr.size());
		// Create a new buffer to hold data about our output image
		cl::Buffer outputImgBuffer(context, CL_MEM_READ_WRITE, inputImgPtr.size()); //should be the same as input image

		// Write normalised cumulative histogram data to our predefined buffer
		queue.enqueueWriteBuffer(normHistBuffer, CL_TRUE, 0, normHistBinSize, &normHistBin[0]);
		
		cl::Kernel kernelLut = cl::Kernel(program, "lut"); // Load the LUT kernel defined in my_kernels
		kernelLut.setArg(0, inputImgBuffer); // Load in our normalised histogram buffer bin
		kernelLut.setArg(1, outputImgBuffer); // Load in our input image in buffer form
		kernelLut.setArg(2, normHistBuffer); // Load in our output image buffer for writing to

		queue.enqueueNDRangeKernel(kernelLut, cl::NullRange, cl::NDRange(inputImgPtr.size()), cl::NullRange);

		//4.3 Copy the result from device to host
		queue.enqueueReadBuffer(outputImgBuffer, CL_TRUE, 0, outputImgVect.size(), & outputImgVect.data()[0]);

		CImg<unsigned char> output_image(outputImgVect.data(), inputImgPtr.width(), inputImgPtr.height(), inputImgPtr.depth(), inputImgPtr.spectrum());
		CImgDisplay outputImgDisp(output_image, "Output Image");

		std::cout << "Kernel execution time [ns]:" << prof_event.getProfilingInfo<CL_PROFILING_COMMAND_END>() - prof_event.getProfilingInfo<CL_PROFILING_COMMAND_START>() << std::endl;
		std::cout << "Profile info: " << GetFullProfilingInfo(prof_event, ProfilingResolution::PROF_US) << std::endl;

		while (!inputImgDisp.is_closed() && !outputImgDisp.is_closed() && !inputImgDisp.is_keyESC() && !outputImgDisp.is_keyESC()) {
			inputImgDisp.wait(1);
			inputImgDisp.wait(1);
		}
	}
	catch (const cl::Error& err) {
		// Handle any exceptions that occur during build/runtime
		std::cerr << "ERROR: " << err.what() << ", " << getErrorString(err.err()) << std::endl;
	}
	catch (CImgException& err) {
		// Handle any CImg related exceptions that occur during build/runtime
		std::cerr << "ERROR: " << err.what() << std::endl;
	}

	// Return 0 to terminate the application
	return 0;
}
