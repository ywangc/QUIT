/*
 *  FitFunction.h - Part of QUantitative Imaging Tools
 *
 *  Copyright (c) 2018 Tobias Wood.
 *
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#ifndef QI_FITFUNCTION_H
#define QI_FITFUNCTION_H

#include "Macro.h"
#include "Model.h"
#include <Eigen/Core>
#include <itkIndex.h>
#include <string>
#include <tuple>

namespace QI {

/*
 *  Return type required by the fit() function objects
 */

struct FitReturnType {
    bool        success;
    std::string message;
};

template <typename Model_, bool Blocked_ = false, bool Indexed_ = false> struct FitFunctionBase {
    using ModelType           = Model_;
    using RMSErrorType        = double;
    static const bool Blocked = Blocked_;
    static const bool Indexed = Indexed_;
    ModelType &       model;
    int               max_iterations = 30;
    int               input_size(const int i) const { return model.input_size(i); }

    FitFunctionBase(ModelType &m) : model{m} {}
};

template <typename ModelType, typename FlagType_ = int>
struct FitFunction : FitFunctionBase<ModelType, false, false> {
    using Super = FitFunctionBase<ModelType, false, false>;
    using Super::Super;
    using typename Super::RMSErrorType;
    using InputType  = typename ModelType::DataType;
    using OutputType = typename ModelType::ParameterType;
    using FlagType   = FlagType_; // Iterations

    int max_iterations = 100;

    virtual FitReturnType fit(const std::vector<QI_ARRAY(InputType)> &inputs,
                              typename ModelType::FixedArray const &  fixed,
                              typename ModelType::VaryingArray &      outputs,
                              typename ModelType::CovarArray *        cov,
                              RMSErrorType &                          rmse,
                              std::vector<QI_ARRAY(InputType)> &      residuals,
                              FlagType &                              flag) const = 0;
};

template <typename ModelType, typename FlagType_ = int>
struct NLLSFitFunction : FitFunction<ModelType> {
    using Super = FitFunction<ModelType, FlagType_>;
    using Super::Super;
    using typename Super::RMSErrorType;
    using InputType    = typename ModelType::DataType;
    using OutputType   = typename ModelType::ParameterType;
    using FlagType     = FlagType_; // Iterations
    int max_iterations = 100;

    FitReturnType fit(const std::vector<QI_ARRAY(InputType)> &inputs,
                      typename ModelType::FixedArray const &  fixed,
                      typename ModelType::VaryingArray &      p,
                      typename ModelType::CovarArray *        cov,
                      RMSErrorType &                          rmse,
                      std::vector<QI_ARRAY(InputType)> &      residuals,
                      FlagType &                              iterations) const {
        auto const &   data = inputs[0];
        ceres::Problem problem;
        using Cost      = ModelCost<ModelType>;
        using AutoCost  = ceres::AutoDiffCostFunction<Cost, ceres::DYNAMIC, ModelType::NV>;
        auto *cost      = new Cost(this->model, fixed, data);
        auto *auto_cost = new AutoCost(cost, this->model.sequence.size());
        problem.AddResidualBlock(auto_cost, NULL, p.data());
        for (int i = 0; i < ModelType::NV; i++) {
            problem.SetParameterLowerBound(p.data(), i, this->model.bounds_lo[i]);
            problem.SetParameterUpperBound(p.data(), i, this->model.bounds_hi[i]);
        }
        ceres::Solver::Options options;
        ceres::Solver::Summary summary;
        options.max_num_iterations  = this->max_iterations;
        options.function_tolerance  = 1e-6;
        options.gradient_tolerance  = 1e-7;
        options.parameter_tolerance = 1e-5;
        options.logging_type        = ceres::SILENT;
        p << this->model.start;
        ceres::Solve(options, &problem, &summary);
        if (!summary.IsSolutionUsable()) {
            return {false, summary.FullReport()};
        }
        iterations = summary.iterations.size();

        Eigen::ArrayXd const rs  = (data - this->model.signal(p, fixed));
        double const         var = rs.square().sum();
        rmse                     = sqrt(var / data.rows());
        if (residuals.size() > 0) {
            residuals[0] = rs;
        }
        if (cov) {
            QI::GetModelCovariance<ModelType>(problem, p, var / (data.rows() - ModelType::NV), cov);
        }

        return {true, ""};
    }
};

template <typename ModelType, typename FlagType_ = int>
struct ScaledNLLSFitFunction : FitFunction<ModelType, FlagType_> {
    using Super = FitFunction<ModelType, FlagType_>;
    using Super::Super;
    using typename Super::RMSErrorType;
    using InputType    = typename ModelType::DataType;
    using OutputType   = typename ModelType::ParameterType;
    using FlagType     = FlagType_; // Iterations
    int max_iterations = 100;

    FitReturnType fit(std::vector<QI_ARRAY(InputType)> const &inputs,
                      typename ModelType::FixedArray const &  fixed,
                      typename ModelType::VaryingArray &      p,
                      typename ModelType::CovarArray *        cov,
                      RMSErrorType &                          rmse,
                      std::vector<QI_ARRAY(InputType)> &      residuals,
                      FlagType &                              iterations) const override {
        const double &scale = inputs[0].maxCoeff();
        if (scale < std::numeric_limits<double>::epsilon()) {
            p    = ModelType::VaryingArray::Zero();
            rmse = 0;
            return {false, "Maximum data value was not positive"};
        }
        const Eigen::ArrayXd data = inputs[0] / scale;
        ceres::Problem       problem;
        using Cost      = ModelCost<ModelType>;
        using AutoCost  = ceres::AutoDiffCostFunction<Cost, ceres::DYNAMIC, ModelType::NV>;
        auto *cost      = new Cost(this->model, fixed, data);
        auto *auto_cost = new AutoCost(cost, this->model.sequence.size());
        problem.AddResidualBlock(auto_cost, NULL, p.data());
        for (int i = 0; i < ModelType::NV; i++) {
            problem.SetParameterLowerBound(p.data(), i, this->model.bounds_lo[i]);
            problem.SetParameterUpperBound(p.data(), i, this->model.bounds_hi[i]);
        }
        ceres::Solver::Options options;
        ceres::Solver::Summary summary;
        options.max_num_iterations  = this->max_iterations;
        options.function_tolerance  = 1e-6;
        options.gradient_tolerance  = 1e-7;
        options.parameter_tolerance = 1e-5;
        options.logging_type        = ceres::SILENT;
        p << this->model.start;
        ceres::Solve(options, &problem, &summary);
        if (!summary.IsSolutionUsable()) {
            return {false, summary.FullReport()};
        }
        iterations               = summary.iterations.size();
        Eigen::ArrayXd const rs  = (data - this->model.signal(p, fixed));
        double const         var = rs.square().sum();
        rmse                     = sqrt(var / data.rows()) * scale;
        if (residuals.size() > 0) {
            residuals[0] = rs * scale;
        }
        if (cov) {
            QI::GetModelCovariance<ModelType>(problem, p, var / (data.rows() - ModelType::NV), cov);
        }
        p[0] = p[0] * scale;
        return {true, ""};
    }
};

template <typename ModelType, typename FlagType_ = int>
struct BlockFitFunction : FitFunctionBase<ModelType, true, false> {
    using Super = FitFunctionBase<ModelType, true, false>;
    using Super::Super;
    using typename Super::RMSErrorType;
    using InputType  = typename ModelType::DataType;
    using OutputType = typename ModelType::ParameterType;
    using FlagType   = FlagType_; // Iterations

    virtual FitReturnType fit(const std::vector<QI_ARRAY(InputType)> &inputs,
                              typename ModelType::FixedArray const &  fixed,
                              typename ModelType::VaryingArray &      outputs,
                              typename ModelType::CovarArray *        cov,
                              RMSErrorType &                          rmse,
                              std::vector<QI_ARRAY(InputType)> &      point_residuals,
                              FlagType &                              flag,
                              const int                               block) const = 0;
};

template <typename ModelType, typename FlagType_ = int>
struct IndexedFitFunction : FitFunctionBase<ModelType, false, true> {
    using Super = FitFunctionBase<ModelType, false, true>;
    using Super::Super;
    using typename Super::RMSErrorType;
    using InputType    = typename ModelType::DataType;
    using OutputType   = typename ModelType::ParameterType;
    using FlagType     = FlagType_; // Iterations
    using SequenceType = typename ModelType::SequenceType;

    virtual FitReturnType fit(const std::vector<QI_ARRAY(InputType)> &inputs,
                              typename ModelType::FixedArray const &  fixed,
                              typename ModelType::VaryingArray &      outputs,
                              typename ModelType::CovarArray *        cov,
                              RMSErrorType &                          rmse,
                              std::vector<QI_ARRAY(InputType)> &      point_residuals,
                              FlagType &                              flag,
                              const itk::Index<3> &                   index) const = 0;
};

} // End namespace QI

#endif // QI_FITFUNCTION_H