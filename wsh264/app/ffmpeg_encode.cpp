#include <iostream>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/wait.h>
#include <opencv2/opencv.hpp>
#include "char_queue.h"

using namespace std;
using namespace cv;

int start_stream(const char *outfile)
{
    // Create pipes for communication with the FFmpeg process
    int stdin_pipe[2];
    int stdout_pipe[2];
    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
      cerr << "Error: Failed to create pipes" << endl;
      return 1;
    }

	// Start the FFmpeg process
	pid_t pid = fork();
	if (pid == 0) {
		// This is the child process (FFmpeg)
		// Close the write end of the stdin pipe and the read end of the stdout pipe
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);

		// Connect the stdin and stdout of the FFmpeg process to the pipes
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);

		// Close the original pipe file descriptors
		close(stdin_pipe[0]);
		close(stdout_pipe[1]);

		// Execute FFmpeg with the desired arguments
		execlp(
			"ffmpeg", "ffmpeg",
			"-loglevel", "quiet",
			"-s", "1920x1080",
			"-f", "rawvideo",
			"-pix_fmt", "yuv420p",
			"-i", "pipe:0",
			"-profile:v", "baseline",
			"-y", // overwrite the exsited output
			"-f", "h264",
			// "pipe:1",
			outfile,
			(char *)nullptr
		);
	} else if (pid > 0) {
		// This is the parent process (server)
		// Close the read end of the stdin pipe and the write end of the stdout pipe
		close(stdin_pipe[0]);
		close(stdout_pipe[1]);

		// Open the webcam using OpenCV
		//VideoCapture webcam(0);
		auto webcam = new VideoCapture(1);
		if (!webcam->isOpened()) {
			cerr << "Error: Failed to open webcam" << endl;
			return 1;
		 }

		// Capture video frames from the webcam and pipe them to FFmpeg
		Mat frame;
		while (webcam->read(frame)) {
			imshow("input", frame);
			Mat yuv;
			cvtColor(frame, yuv, COLOR_BGR2YUV_I420);

			// Write the frame data to the stdin pipe
			if (write(stdin_pipe[1], yuv.data, yuv.total()) < 0) {
				cerr << "Error: Failed to write to stdin pipe" << endl;
				break;
			}
			if (waitKey(1) == 27)
				break;
		}
		delete webcam;

		// Close the stdin pipe
		close(stdin_pipe[1]);
		int status;
		waitpid(pid, &status, 0); // wait for the ffmpeg process to finish
	} else {
		cerr << "Error: Failed to fork()" << endl;
		return 1;
	}

  return 0;
}

int main()
{
	start_stream("output.h264");
}
