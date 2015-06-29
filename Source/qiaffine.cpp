/*
 *  qiaffine.cpp
 *
 *  Copyright (c) 2015 Tobias Wood.
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <getopt.h>
#include <iostream>

#include "itkImage.h"
#include "itkVersor.h"
#include "itkChangeInformationImageFilter.h"

#include "Util.h"
#include "Types.h"

using namespace std;

int main(int argc, char **argv) {
	const string usage {
	"Usage is: qiaffine input [output] [transforms] \n\
	\n\
	Applies simple affine transformations to images by manipulating the header\n\
	transforms. If an output file is not specified, the input file will be\n\
	overwritten.\n\
	\n\
	Transform Options:\n\
		--rotX N : Rotate about the X axis by N degrees\n\
		--rotY N : Rotate about the Y axis by N degrees\n\
		--rotZ N : Rotate about the Z axis by N degrees\n\
	\n\
	Other Options:\n\
		--help, -h    : Print this message\n\
		--verbose, -v : Print more messages\n\
	\n"};

	int verbose = false;
	const struct option long_options[] =
	{
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{"rotX", required_argument, 0, 'X'},
		{"rotY", required_argument, 0, 'Y'},
		{"rotZ", required_argument, 0, 'Z'},
		{0, 0, 0, 0}
	};
	const char* short_options = "hv";
	int c, index_ptr = 0;

	// Do one pass to get options in the right order
	while ((c = getopt_long(argc, argv, short_options, long_options, &index_ptr)) != -1) {
		switch (c) {
			case 'v': verbose = true; break;
			case 'h':
				cout << usage << endl;
				return EXIT_SUCCESS;
			case 'X': case 'Y': case 'Z':
			case 0: break; // A flag
			case '?': // getopt will print an error message
				return EXIT_FAILURE;
			default:
			cout << "Unhandled option " << string(1, c) << endl;
			return EXIT_FAILURE;
		}
	}

	if (((argc - optind) == 0) || ((argc - optind) > 2)) {
		cout << usage << endl;
		cout << "Incorrect number of arguments" << endl;
		return EXIT_FAILURE;
	}

	// Now read in the input image
	auto reader = QI::ReadTimeseriesF::New();
	reader->SetFileName(argv[optind]);
	reader->Update();
	auto image = reader->GetOutput();
	auto writer = QI::WriteTimeseriesF::New();
	if ((argc - optind) == 2) {
		optind++;
	}
	writer->SetFileName(argv[optind]);

	// Now reset optind and actually process
	QI::TimeseriesF::DirectionType fullDir = image->GetDirection();
	QI::TimeseriesF::SpacingType fullSpacing = image->GetSpacing();
	QI::TimeseriesF::PointType fullOrigin = image->GetOrigin();
	QI::ImageF::DirectionType direction;
	QI::ImageF::SpacingType spacing;
	QI::ImageF::PointType origin;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			direction[i][j] = fullDir[i][j];
		}
		origin[i] = fullOrigin[i];
	}
	optind = 1;
	while ((c = getopt_long(argc, argv, short_options, long_options, &index_ptr)) != -1) {
		switch (c) {
			// Already handled these
			case 'v': case 'h': case 0: break;
			case 'X': {
				if (verbose) cout << "Rotating image by " << string(optarg) << " around X axis." << endl;
				const double radians = atof(optarg) * vnl_math::pi / 180.0;
				itk::Versor<double> rotate; rotate.SetRotationAroundX(radians);
				direction = rotate.GetMatrix() * direction;
				origin = rotate.GetMatrix() * origin;
			} break;
			case 'Y': {
				if (verbose) cout << "Rotating image by " << string(optarg) << " around Y axis." << endl;
				const double radians = atof(optarg) * vnl_math::pi / 180.0;
				itk::Versor<double> rotate; rotate.SetRotationAroundY(radians);
				direction = rotate.GetMatrix() * direction;
				origin = rotate.GetMatrix() * origin;
			} break;
			case 'Z': {
				if (verbose) cout << "Rotating image by " << string(optarg) << " around Z axis." << endl;
				const double radians = atof(optarg) * vnl_math::pi / 180.0;
				itk::Versor<double> rotate; rotate.SetRotationAroundZ(radians);
				direction = rotate.GetMatrix() * direction;
				origin = rotate.GetMatrix() * origin;
			} break;
		}
	}
	auto changeInfo = itk::ChangeInformationImageFilter<QI::TimeseriesF>::New();
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			fullDir[i][j] = direction[i][j];
		}
		fullOrigin[i] = origin[i];
	}
	changeInfo->SetOutputDirection(fullDir);
	changeInfo->SetOutputOrigin(fullOrigin);
	changeInfo->ChangeDirectionOn();
	changeInfo->ChangeOriginOn();
	changeInfo->SetInput(image);
	writer->SetInput(changeInfo->GetOutput());
	writer->Update();
	return EXIT_SUCCESS;
}
