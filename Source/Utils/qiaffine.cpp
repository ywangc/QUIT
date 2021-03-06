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

#include "itkCenteredAffineTransform.h"
#include "itkEuler3DTransform.h"
#include "itkFlipImageFilter.h"
#include "itkImageIOFactory.h"
#include "itkImageMomentsCalculator.h"
#include "itkPermuteAxesImageFilter.h"
#include "itkTransformFileWriter.h"
#include "itkVersor.h"
#include "itkVersorRigid3DTransform.h"

#include "Args.h"
#include "ImageIO.h"
#include "Util.h"

int affine_main(args::Subparser &parser) {
    args::Positional<std::string> source_path(parser, "SOURCE", "Source file");
    args::Positional<std::string> dest_path(parser, "DEST", "Destination file");

    args::ValueFlag<std::string> center(
        parser, "CENTER", "Set the origin to geometric center (geo) or (cog)", {'c', "center"});
    args::ValueFlag<std::string> tfm_path(
        parser, "TFM", "Write out the transformation to a file", {'t', "tfm"});
    args::ValueFlag<std::string> permute(
        parser,
        "PERMUTE",
        "Permute axes in data-space, e.g. 2,0,1. Negative values mean flip as well",
        {"permute"});
    args::ValueFlag<std::string> flip(
        parser,
        "FLIP",
        "Flip axes in data-space, e.g. 0,1,0. Occurs AFTER any permutation.",
        {"flip"});
    args::ValueFlag<double>      scale(parser, "SCALE", "Scale by a constant", {'s', "scale"}, 1.);
    args::ValueFlag<std::string> translate(
        parser, "TRANSLATE", "Translate image by X,Y,Z (mm)", {"trans"}, "0,0,0");
    args::ValueFlag<std::string> rotate(
        parser, "ROTATE", "Rotate by Euler angles around X,Y,Z (degrees).", {"rotate"}, "0,0,0");
    parser.Parse();
    QI::Log(verbose, "Reading header for: {}", QI::CheckPos(source_path));
    auto header = itk::ImageIOFactory::CreateImageIO(QI::CheckPos(source_path).c_str(),
                                                     itk::ImageIOFactory::ReadMode);
    if (!header) {
        QI::Fail("Failed to read header from: {}", source_path.Get());
    }
    header->SetFileName(source_path.Get());
    header->ReadImageInformation();
    auto dims  = header->GetNumberOfDimensions();
    auto dtype = header->GetComponentType();
    QI::Log(verbose, "Datatype is {}", header->GetComponentTypeAsString(dtype));

    auto pipeline = [&]<typename T, int N>() {
        using TImage = itk::Image<T, N>;
        auto image   = QI::ReadImage<TImage>(QI::CheckPos(source_path), verbose);

        // Permute if required
        if (permute) {
            auto permute_filter = itk::PermuteAxesImageFilter<TImage>::New();
            itk::FixedArray<unsigned int, TImage::ImageDimension> permute_order;
            std::istringstream                                    iss(permute.Get());
            std::string                                           el;
            for (int i = 0; i < 3; i++) {
                std::getline(iss, el, ',');
                permute_order[i] = std::stoi(el);
            }
            for (size_t i = 3; i < TImage::ImageDimension; i++) {
                permute_order[i] = i;
            }
            if (!iss)
                QI::Fail("Failed to read permutation order: {}", permute.Get());

            permute_filter->SetInput(image);
            permute_filter->SetOrder(permute_order);
            permute_filter->Update();
            image = permute_filter->GetOutput();
            image->DisconnectPipeline();
        }

        // Get these before flipping otherwise header info is flipped too
        typename TImage::DirectionType fullDir     = image->GetDirection();
        typename TImage::SpacingType   fullSpacing = image->GetSpacing();
        typename TImage::PointType     fullOrigin  = image->GetOrigin();
        typename TImage::SizeType      fullSize    = image->GetLargestPossibleRegion().GetSize();
        QI::VolumeF::DirectionType     direction;
        QI::VolumeF::SpacingType       spacing;

        // Flip if required
        if (flip) {
            auto flip_filter = itk::FlipImageFilter<TImage>::New();
            itk::FixedArray<bool, TImage::ImageDimension> flip_axes; // Save this
            std::istringstream                            iss(flip.Get());
            std::string                                   el;
            for (size_t i = 0; i < 3; i++) {
                std::getline(iss, el, ',');
                flip_axes[i] = (std::stoi(el) > 0);
            }
            for (size_t i = 3; i < TImage::ImageDimension; i++) {
                flip_axes[i] = false;
            }
            if (!iss)
                QI::Fail("Failed to read flip: {}", flip.Get());
            QI::Log(verbose, "Flipping: {}", flip_axes);
            flip_filter->SetInput(image);
            flip_filter->SetFlipAxes(flip_axes);
            flip_filter->SetFlipAboutOrigin(false);
            flip_filter->Update();
            image = flip_filter->GetOutput();
            image->DisconnectPipeline();
        }

        using Affine = itk::CenteredAffineTransform<double, 3>;
        using Euler  = itk::Euler3DTransform<double>;
        Affine::OutputVectorType origin;
        QI::VolumeF::SizeType    size;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                direction[i][j] = fullDir[i][j];
            }
            origin[i]  = fullOrigin[i];
            spacing[i] = fullSpacing[i];
            size[i]    = fullSize[i];
        }

        auto img_tfm = Affine::New();
        img_tfm->SetMatrix(direction);
        img_tfm->Scale(spacing);
        img_tfm->Translate(origin);
        if (scale) {
            QI::Log(verbose, "Scaling by factor {}", scale.Get());
            img_tfm->Scale(scale.Get());
        }

        auto tfm = Euler::New();
        if (rotate) {
            Eigen::Array3d angles;
            QI::ArrayArg<Eigen::Array3d, 3>(rotate.Get(), angles);
            tfm->SetRotation(
                angles[0] * M_PI / 180., angles[1] * M_PI / 180., angles[2] * M_PI / 180.);
            QI::Log(verbose, "Rotation by: {}", angles.transpose());
        }
        if (translate) {
            Euler::OffsetType offset;
            QI::ArrayArg<Euler::OffsetType, 3>(translate.Get(), offset);
            tfm->Translate(-offset);
            QI::Log(verbose, "Translate by: {}", offset);
        }
        if (center) {
            Euler::OffsetType offset;
            offset.Fill(0.);
            if (center.Get() == "geo") {
                QI::Log(verbose, "Setting geometric center");
                for (int i = 0; i < 3; i++) {
                    offset[i] = origin[i] - spacing[i] * size[i] / 2;
                }
            } else if (center.Get() == "cog") {
                QI::Log(verbose, "Setting center to center of gravity");
                auto moments = itk::ImageMomentsCalculator<TImage>::New();
                moments->SetImage(image);
                moments->Compute();
                // ITK seems to put a negative sign on the CoG
                for (int i = 0; i < 3; i++) {
                    offset[i] = moments->GetCenterOfGravity()[i];
                }
            }
            std::cout << "Translation will be: " << offset << std::endl;
            tfm->Translate(-offset);
        }

        if (tfm_path) { // Output the transform file
            auto writer = itk::TransformFileWriterTemplate<double>::New();
            writer->SetInput(tfm);
            writer->SetFileName(tfm_path.Get());
            QI::Log(verbose, "Writing transform file: {}", tfm_path.Get());
            writer->Update();
        }

        img_tfm->Compose(tfm);
        itk::CenteredAffineTransform<double, 3>::MatrixType fmat = img_tfm->GetMatrix();
        QI::Log(verbose, "Final transform:\n{}", fmat);
        for (int i = 0; i < 3; i++) {
            fullOrigin[i] = img_tfm->GetOffset()[i];
        }
        for (int j = 0; j < 3; j++) {
            double mat_scale = 0.;
            for (int i = 0; i < 3; i++) {
                mat_scale += fmat[i][j] * fmat[i][j];
            }
            mat_scale = sqrt(mat_scale);
            for (int i = 0; i < 3; i++) {
                fullDir[i][j] = fmat[i][j] / mat_scale;
            }
        }
        image->SetDirection(fullDir);
        image->SetOrigin(fullOrigin);
        image->SetSpacing(fullSpacing * scale.Get());

        // Write out the edited file
        if (dest_path) {
            QI::WriteImage(image, dest_path.Get(), verbose);
        } else {
            QI::WriteImage(image, source_path.Get(), verbose);
        }
    };

    if (dims < 3 || dims > 4) {
        QI::Fail("Unsupported number of dimensions {}", dims);
    }

    switch (dtype) {
    case itk::ImageIOBase::UNKNOWNCOMPONENTTYPE:
        QI::Fail("Unknown component type in image {}", source_path);
    /*case itk::ImageIOBase::UCHAR:  DIM_SWITCH( unsigned char ); break;
    case itk::ImageIOBase::CHAR:   DIM_SWITCH( char ); break;
    case itk::ImageIOBase::USHORT: DIM_SWITCH( unsigned short ); break;
    case itk::ImageIOBase::SHORT:  DIM_SWITCH( short ); break;
    case itk::ImageIOBase::UINT:   DIM_SWITCH( unsigned int ); break;
    case itk::ImageIOBase::INT:    DIM_SWITCH( int ); break;
    case itk::ImageIOBase::ULONG:  DIM_SWITCH( unsigned long ); break;
    case itk::ImageIOBase::LONG:   DIM_SWITCH( long ); break;
    case itk::ImageIOBase::LONGLONG: DIM_SWITCH( long long ); break;
    case itk::ImageIOBase::ULONGLONG: DIM_SWITCH( unsigned long long ); break;*/
    case itk::ImageIOBase::FLOAT:
        switch (dims) {
        case 3:
            pipeline.operator()<float, 3>();
            break;
        case 4:
            pipeline.operator()<float, 4>();
            break;
        }
    case itk::ImageIOBase::DOUBLE:
        switch (dims) {
        case 3:
            pipeline.operator()<double, 3>();
            break;
        case 4:
            pipeline.operator()<double, 4>();
            break;
        }
    default:
        QI::Fail("Unimplemented component type: {} in image {}", dtype, source_path);
    }
    return EXIT_SUCCESS;
}
