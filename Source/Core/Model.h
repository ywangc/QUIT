/*
 *  Model.h - Part of QUantitative Imaging Tools
 *
 *  Copyright (c) 2018 Tobias Wood.
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#pragma once

#include "ImageTypes.h"
#include "Macro.h"
#include "ceres/ceres.h"
#include <array>
#include <string>

namespace QI {

/*
 *  Standard Model interface
 */
template <typename DT_, typename PT_, int NV_, int NF_, int NI_ = 1, int ND_ = 0> struct Model {
    using DataType      = DT_;
    using ParameterType = PT_;

    static constexpr int NV = NV_; // Number of varying parameters
    static constexpr int NF = NF_; // Number of fixed parameters (fixed per voxel, e.g. B1)
    static constexpr int ND = ND_; // Number of derived parameters (calculated from varying)
    static constexpr int NI = NI_; // Number of inputs
    static constexpr int NCov =
        NV * (NV + 1) / 2; // Number of parameter variance/covariance entries

    using VaryingArray = QI_ARRAYN(ParameterType, NV);
    using FixedArray   = QI_ARRAYN(ParameterType, NF);
    using CovarArray   = QI_ARRAYN(ParameterType, NCov);
    using DerivedArray = QI_ARRAYN(ParameterType, ND);

    using VaryingNames = std::array<std::string const, NV>;
    using FixedNames   = std::array<std::string const, NF>;
    using DerivedNames = std::array<std::string const, ND>;

    template <typename Derived>
    auto signal(const Eigen::ArrayBase<Derived> &varying, const FixedArray &fixed) const
        -> QI_ARRAY(typename Derived::Scalar);
};

/*
 *  Convert the Covariance Matrix from Ceres into something useful
 * The diagonal elements are the estimation variance of each parameter (after division by the
 * residual). Square-root to get the standard deviation. Off-diagonal elements need to be divided by
 * the standard deviation of each variable to get the correlation.
 */
template <typename Model>
void GetModelCovariance(ceres::Problem &                    p,
                        typename Model::VaryingArray const &v,
                        double const &                      scale,
                        typename Model::CovarArray *        ptr) {
    ceres::Covariance::Options cov_options;
    ceres::Covariance          cov_c(cov_options);
    cov_c.Compute({std::make_pair(v.data(), v.data())}, &p);

    Eigen::MatrixXd full(Model::NV, Model::NV);
    cov_c.GetCovarianceBlock(v.data(), v.data(), full.data());
    full *= scale;
    typename Model::CovarArray &cov = (*ptr);
    cov.head(Model::NV)             = full.diagonal().array().sqrt();
    int index                       = Model::NV;
    for (int ii = 0; ii < Model::NV; ii++) {
        for (int jj = ii + 1; jj < Model::NV; jj++) {
            cov[index++] = full(ii, jj) / (cov[ii] * cov[jj]);
        }
    }
    cov.head(Model::NV) /= v;
    QI_DBMAT(full);
    QI_DBVEC(cov);
}

/*
 *  A generic Ceres Cost Function compatible with auto-differentation
 */
template <typename Model> struct ModelCost {
    using VaryingArray = typename Model::VaryingArray;
    using FixedArray   = typename Model::FixedArray;
    using DataArray    = QI_ARRAY(typename Model::DataType);
    const Model &    model;
    const FixedArray fixed;
    const DataArray  data;

    template <typename T> bool operator()(const T *const vin, T *rin) const {
        Eigen::Map<QI_ARRAYN(T, Model::NV) const> const v(vin);

        auto const signal = model.signal(v, fixed);

        Eigen::Map<QI_ARRAY(T)> residual(rin, data.rows());
        residual = data - signal;
        QI_DBVEC(data);
        QI_DBVEC(signal);
        QI_DBVEC(residual);

        return true;
    }
};

/*
 *  Helper struct for converting between double/float for processing & IO
 */
template <typename DataType> struct IOPrecision;

template <> struct IOPrecision<double> { using Type = float; };

template <> struct IOPrecision<std::complex<double>> { using Type = std::complex<float>; };

/*
 *  Helper struct for blocked filter output types
 */
template <bool Blocked, int ImageDimension, typename T> struct BlockTypes {};

template <int ImageDimension, typename T> struct BlockTypes<true, ImageDimension, T> {
    using Type = itk::VectorImage<T, ImageDimension>;
};

template <int ImageDimension, typename T> struct BlockTypes<false, ImageDimension, T> {
    using Type = itk::Image<T, ImageDimension>;
};

/*
 *  Which noise type to choose
 */
template <typename DataType> struct NoiseFromDataType;

template <> struct NoiseFromDataType<double> {
    static Eigen::ArrayXd add_noise(Eigen::ArrayXd const &s, double const sigma);
};

template <> struct NoiseFromDataType<std::complex<double>> {
    static Eigen::ArrayXcd add_noise(Eigen::ArrayXcd const &s, double const sigma);
};

struct RealNoise {
    static Eigen::ArrayXd add_noise(Eigen::ArrayXd const &s, double const sigma);
};

template <typename ModelType>
struct NoiseFromModelType : NoiseFromDataType<typename ModelType::DataType> {};

} // End namespace QI
