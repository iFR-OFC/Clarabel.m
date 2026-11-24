// clarabel_mex.cpp
//
// MATLAB C++ MEX interface for the Clarabel interior-point solver.
//
// Usage:
//   solution = clarabel_mex(P, q, A, b, cones) or
//   solution = clarabel_mex(P, q, A, b, cones,settings) where settings is either
//   settings  = DefaultSettings() or you can set single valued fields
//   Note: DefaultSettings.m is a mirror of the default parameter
//
// Inputs:
//   P        sparse or dense n x n matrix   (quadratic cost: 1/2 x'Px + q'x)
//   q        n x 1 vector
//   A        sparse or dense m x n matrix  (constraints: Ax + s = b, s in K)
//   b        m x 1 vector
//   cones    struct array built by the functions in DefaultCones/
//            (zeroConeT, NonnegativeConeT, SecondOrderConeT, exponentialConeT,
//             PowerConeT, GenPowerConeT, PSDTriangleConeT)
//   settings optional struct from DefaultSettings.m; absent fields keep C++ defaults
//
// Output:
//   solution struct with fields
//       x, z, s          primal/dual solution vectors
//       status           solver status string
//       obj_val          primal objective
//       obj_val_dual     dual objective
//       solve_time       solve time in seconds
//       iterations       number of ADMM/IPM iterations
//       r_prim, r_dual   primal/dual residuals
//
// Build with make_clarabel_mex.m (see that file for prerequisites).

#include "mex.hpp"
#include "mexAdapter.hpp"
#include "MatlabDataArray.hpp"

#include <Eigen/Dense>
#include <Eigen/Sparse>

#include "clarabel.hpp"

#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace clarabel;
using matlab::mex::ArgumentList;
using matlab::data::Array;
using matlab::data::ArrayFactory;
using matlab::data::ArrayType;
using matlab::data::CharArray;
using matlab::data::MATLABString;
using matlab::data::StructArray;
using matlab::data::TypedArray;

namespace {

// Map a Clarabel solver status to its MATLAB-facing string.
const char* statusToString(SolverStatus status)
{
    switch (status) {
        case SolverStatus::Unsolved:               return "Unsolved";
        case SolverStatus::Solved:                 return "Solved";
        case SolverStatus::PrimalInfeasible:       return "PrimalInfeasible";
        case SolverStatus::DualInfeasible:          return "DualInfeasible";
        case SolverStatus::AlmostSolved:           return "AlmostSolved";
        case SolverStatus::AlmostPrimalInfeasible: return "AlmostPrimalInfeasible";
        case SolverStatus::AlmostDualInfeasible:   return "AlmostDualInfeasible";
        case SolverStatus::MaxIterations:          return "MaxIterations";
        case SolverStatus::MaxTime:                return "MaxTime";
        case SolverStatus::NumericalError:         return "NumericalError";
        case SolverStatus::InsufficientProgress:   return "InsufficientProgress";
        default:                                   return "Unknown";
    }
}

} // namespace

class MexFunction : public matlab::mex::Function {
private:
    std::shared_ptr<matlab::engine::MATLABEngine> matlabPtr_;
    ArrayFactory factory_;

    // ------------------------------------------------------------------
    // MATLAB interaction helpers
    // ------------------------------------------------------------------

    void raiseError(const std::string& msg)
    {
        matlabPtr_->feval(u"error", 0,
            std::vector<Array>({factory_.createScalar(msg)}));
    }

    void raiseWarning(const std::string& msg)
    {
        matlabPtr_->feval(u"warning", 0,
            std::vector<Array>({factory_.createScalar(msg)}));
    }

    // Accept either a char array ('FAER') or a MATLAB string ("FAER").
    std::string getString(const Array& arr, const char* name)
    {
        if (arr.getType() == ArrayType::CHAR) {
            CharArray chars = arr;
            return chars.toAscii();
        }
        if (arr.getType() == ArrayType::MATLAB_STRING) {
            TypedArray<MATLABString> strings = arr;
            MATLABString str = strings[0];
            if (str.has_value()) {
                return u16toAscii(*str);
            }
        }
        raiseError(std::string(name) + " must be a string.");
        return "";
    }

    static std::string u16toAscii(const std::u16string& u16str)
    {
        std::string result;
        result.reserve(u16str.size());
        for (char16_t c : u16str) {
            result += static_cast<char>(c);
        }
        return result;
    }

    static std::string toLower(std::string str)
    {
        for (char& c : str) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return str;
    }

    // ------------------------------------------------------------------
    // Input conversion (MATLAB -> Eigen)
    // ------------------------------------------------------------------

    Eigen::VectorXd toVector(const Array& arr, const char* name)
    {
        if (arr.getType() != ArrayType::DOUBLE) {
            raiseError(std::string(name) + " must be a double vector.");
        }
        TypedArray<double> typed = arr;
        Eigen::VectorXd vec(typed.getNumberOfElements());
        size_t idx = 0;
        for (double elem : typed) {
            vec(idx++) = elem; // MATLAB arrays iterate in column-major order
        }
        return vec;
    }

    Eigen::SparseMatrix<double> toSparse(const Array& arr, const char* name)
    {
        auto dims = arr.getDimensions();
        if (dims.size() != 2) {
            raiseError(std::string(name) + " must be a matrix.");
        }
        size_t rows = dims[0];
        size_t cols = dims[1];

        if (arr.getType() == ArrayType::SPARSE_DOUBLE) {
            // The C++ MEX API does not expose sparse indices directly, so
            // extract (i, j, v) triplets via MATLAB's find().
            auto result = matlabPtr_->feval(u"find", 3,
                std::vector<Array>({arr}));

            TypedArray<double> rowArr = result[0];
            TypedArray<double> colArr = result[1];
            TypedArray<double> valArr = result[2];

            std::vector<Eigen::Triplet<double>> triplets;
            triplets.reserve(rowArr.getNumberOfElements());
            for (size_t k = 0; k < rowArr.getNumberOfElements(); ++k) {
                triplets.emplace_back(
                    static_cast<size_t>(rowArr[k]) - 1, // MATLAB is 1-indexed
                    static_cast<size_t>(colArr[k]) - 1,
                    valArr[k]);
            }

            Eigen::SparseMatrix<double> mat(rows, cols);
            mat.setFromTriplets(triplets.begin(), triplets.end());
            return mat;
        }

        if (arr.getType() == ArrayType::DOUBLE) {
            // Dense input: copy elementwise, then drop explicit zeros.
            TypedArray<double> dense = arr;
            Eigen::MatrixXd tmp(rows, cols);
            for (size_t j = 0; j < cols; ++j) {
                for (size_t i = 0; i < rows; ++i) {
                    tmp(i, j) = dense[i][j];
                }
            }
            return tmp.sparseView();
        }

        raiseError(std::string(name) + " must be a double matrix (sparse or dense).");
        return Eigen::SparseMatrix<double>(); // unreachable
    }

    // ------------------------------------------------------------------
    // Struct field readers
    // ------------------------------------------------------------------

    // Read the first element of a numeric or logical field as a double.
    double readScalar(const Array& arr, const char* name)
    {
        switch (arr.getType()) {
            case ArrayType::DOUBLE:   { TypedArray<double>  a = arr; return a[0]; }
            case ArrayType::SINGLE:   { TypedArray<float>   a = arr; return static_cast<double>(a[0]); }
            case ArrayType::INT32:    { TypedArray<int32_t> a = arr; return static_cast<double>(a[0]); }
            case ArrayType::UINT32:   { TypedArray<uint32_t>a = arr; return static_cast<double>(a[0]); }
            case ArrayType::LOGICAL:  { TypedArray<bool>    a = arr; return a[0] ? 1.0 : 0.0; }
            default:
                raiseError(std::string(name) + " must be numeric or logical.");
        }
        return 0.0; // unreachable
    }

    bool hasField(const StructArray& s, const char* field)
    {
        for (const auto& name : s.getFieldNames()) {
            if (static_cast<std::string>(name) == field) {
                return true;
            }
        }
        return false;
    }

    void setIfPresent(const StructArray& s, const char* field, bool& target)
    {
        if (hasField(s, field)) {
            target = readScalar(s[0][field], field) != 0.0;
        }
    }

    void setIfPresent(const StructArray& s, const char* field, uint32_t& target)
    {
        if (hasField(s, field)) {
            double value = readScalar(s[0][field], field);
            if (value < 0.0) {
                raiseError(std::string(field) + " must be non-negative.");
            }
            target = static_cast<uint32_t>(value);
        }
    }

    void setIfPresent(const StructArray& s, const char* field, double& target)
    {
        if (hasField(s, field)) {
            target = readScalar(s[0][field], field);
        }
    }

    // ------------------------------------------------------------------
    // Cone parsing
    // ------------------------------------------------------------------

    // A parsed cone list. GenPowerConeT stores a raw pointer into the alpha
    // data, so the alpha vectors are kept alongside the cones and must
    // outlive any DefaultSolver constructed from them.
    struct ConeSpec {
        std::vector<SupportedConeT<double>> cones;
        std::vector<Eigen::VectorXd> alphaStorage;
    };

    ConeSpec parseCones(const Array& input)
    {
        if (input.getType() != ArrayType::STRUCT) {
            raiseError("Cones input must be a struct array (see DefaultCones/).");
        }
        StructArray cones = input;

        ConeSpec spec;
        spec.cones.reserve(cones.getNumberOfElements());
        // Capacity is reserved up front so that the stored alpha buffers
        // are never reallocated.
        spec.alphaStorage.reserve(cones.getNumberOfElements());

        for (size_t i = 0; i < cones.getNumberOfElements(); ++i) {
            std::string name = getString(cones[i]["coneName"], "coneName");
            size_t dim = static_cast<size_t>(readScalar(cones[i]["dim"], "dim"));

            // Cones parameterized by a dimension. Empty cones are skipped.
            if (name == "ZeroCone" || name == "NonnegativeCone" ||
                name == "SecondOrderCone" || name == "PSDTriangleCone") {
                if (dim == 0) {
                    continue;
                }
                if (name == "ZeroCone") {
                    spec.cones.push_back(ZeroConeT<double>(dim));
                } else if (name == "NonnegativeCone") {
                    spec.cones.push_back(NonnegativeConeT<double>(dim));
                } else if (name == "SecondOrderCone") {
                    spec.cones.push_back(SecondOrderConeT<double>(dim));
#ifdef FEATURE_SDP
                } else {
                    spec.cones.push_back(PSDTriangleConeT<double>(dim));
                }
#else
                } else {
                    raiseError("PSDTriangleCone requires a MEX built with -DFEATURE_SDP.");
                }
#endif
            } else if (name == "ExponentialCone") {
                spec.cones.push_back(ExponentialConeT<double>());
            } else if (name == "PowerCone") {
                // The scalar is the cone power, not a dimension (abused
                // 'dim' field), so it must be read without truncation.
                double power = readScalar(cones[i]["dim"], "dim");
                spec.cones.push_back(PowerConeT<double>(power));
            } else if (name == "GenPowerCone") {
                spec.alphaStorage.push_back(toVector(cones[i]["alpha"], "alpha"));
                spec.cones.push_back(
                    GenPowerConeT<double>(spec.alphaStorage.back(), dim));
            } else {
                raiseError("Unknown cone name: " + name);
            }
        }
        return spec;
    }

    // ------------------------------------------------------------------
    // Settings parsing
    // ------------------------------------------------------------------

    DefaultSettings<double> mexDefaultSettings()
    {
        DefaultSettings<double> settings = DefaultSettings<double>::default_settings();
        settings.verbose = false;
        return settings;
    }

    DefaultSettings<double> parseSettings(const Array& input)
    {
        if (input.getNumberOfElements() == 0) {
            return mexDefaultSettings();
        }
        if (input.getType() != ArrayType::STRUCT) {
            raiseError("Settings input must be a struct (see DefaultSettings.m), empty, or omitted.");
        }
        StructArray s = input;

        DefaultSettings<double> settings = mexDefaultSettings();

        // Basic
        setIfPresent(s, "verbose", settings.verbose);
        setIfPresent(s, "max_iter", settings.max_iter);
        setIfPresent(s, "time_limit", settings.time_limit);
        setIfPresent(s, "max_threads", settings.max_threads);
        setIfPresent(s, "presolve_enable", settings.presolve_enable);

        // Scaling / equilibration
        setIfPresent(s, "equilibrate_enable", settings.equilibrate_enable);
        setIfPresent(s, "equilibrate_max_iter", settings.equilibrate_max_iter);
        setIfPresent(s, "equilibrate_min_scaling", settings.equilibrate_min_scaling);
        setIfPresent(s, "equilibrate_max_scaling", settings.equilibrate_max_scaling);

        // Termination tolerances
        setIfPresent(s, "tol_gap_abs", settings.tol_gap_abs);
        setIfPresent(s, "tol_gap_rel", settings.tol_gap_rel);
        setIfPresent(s, "tol_feas", settings.tol_feas);
        setIfPresent(s, "tol_infeas_abs", settings.tol_infeas_abs);
        setIfPresent(s, "tol_infeas_rel", settings.tol_infeas_rel);
        setIfPresent(s, "tol_ktratio", settings.tol_ktratio);
        setIfPresent(s, "reduced_tol_gap_abs", settings.reduced_tol_gap_abs);
        setIfPresent(s, "reduced_tol_gap_rel", settings.reduced_tol_gap_rel);
        setIfPresent(s, "reduced_tol_feas", settings.reduced_tol_feas);
        setIfPresent(s, "reduced_tol_infeas_abs", settings.reduced_tol_infeas_abs);
        setIfPresent(s, "reduced_tol_infeas_rel", settings.reduced_tol_infeas_rel);
        setIfPresent(s, "reduced_tol_ktratio", settings.reduced_tol_ktratio);

        // Step size / linesearch
        setIfPresent(s, "max_step_fraction", settings.max_step_fraction);
        setIfPresent(s, "linesearch_backtrack_step", settings.linesearch_backtrack_step);
        setIfPresent(s, "min_switch_step_length", settings.min_switch_step_length);
        setIfPresent(s, "min_terminate_step_length", settings.min_terminate_step_length);

        // KKT solver
        setIfPresent(s, "direct_kkt_solver", settings.direct_kkt_solver);
        if (hasField(s, "direct_solve_method")) {
            std::string solveMethod = toLower(getString(s[0]["direct_solve_method"],
                                            "direct_solve_method"));
            if (solveMethod == "auto") {
                settings.direct_solve_method = ClarabelDirectSolveMethods::AUTO;
            } else if (solveMethod == "qdldl") {
                settings.direct_solve_method = ClarabelDirectSolveMethods::QDLDL;
#ifdef FEATURE_FAER_SPARSE
            } else if (solveMethod == "faer") {
                settings.direct_solve_method = ClarabelDirectSolveMethods::FAER;
#else
            } else if (solveMethod == "faer") {
                raiseWarning("FAER requested, but this MEX was built without FAER support. Using QDLDL.");
                settings.direct_solve_method = ClarabelDirectSolveMethods::QDLDL;
#endif
            } else {
                raiseError("Unknown direct_solve_method: " + solveMethod);
            }
        }

        // Static/dynamic regularization
        setIfPresent(s, "static_regularization_enable", settings.static_regularization_enable);
        setIfPresent(s, "static_regularization_constant", settings.static_regularization_constant);
        setIfPresent(s, "static_regularization_proportional", settings.static_regularization_proportional);
        setIfPresent(s, "dynamic_regularization_enable", settings.dynamic_regularization_enable);
        setIfPresent(s, "dynamic_regularization_eps", settings.dynamic_regularization_eps);
        setIfPresent(s, "dynamic_regularization_delta", settings.dynamic_regularization_delta);

        // Iterative refinement
        setIfPresent(s, "iterative_refinement_enable", settings.iterative_refinement_enable);
        setIfPresent(s, "iterative_refinement_max_iter", settings.iterative_refinement_max_iter);
        setIfPresent(s, "iterative_refinement_reltol", settings.iterative_refinement_reltol);
        setIfPresent(s, "iterative_refinement_abstol", settings.iterative_refinement_abstol);
        setIfPresent(s, "iterative_refinement_stop_ratio", settings.iterative_refinement_stop_ratio);

#ifdef FEATURE_SDP
        // Chordal decomposition (SDP problems only)
        setIfPresent(s, "chordal_decomposition_enable", settings.chordal_decomposition_enable);
        setIfPresent(s, "chordal_decomposition_compact", settings.chordal_decomposition_compact);
        setIfPresent(s, "chordal_decomposition_complete_dual", settings.chordal_decomposition_complete_dual);

        if (hasField(s, "chordal_decomposition_merge_method")) {
            std::string mergeMethod = toLower(getString(s[0]["chordal_decomposition_merge_method"],
                                            "chordal_decomposition_merge_method"));
            if (mergeMethod == "clique_graph") {
                settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::CLIQUE_GRAPH;
            } else if (mergeMethod == "parent_child") {
                settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::PARENT_CHILD;
            } else if (mergeMethod == "none") {
                settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::NONE;
            } else {
                raiseError("Unknown chordal_decomposition_merge_method: " + mergeMethod);
            }
        }
#endif

        return settings;
    }

    // ------------------------------------------------------------------
    // Output conversion (solution -> MATLAB struct)
    // ------------------------------------------------------------------

    TypedArray<double> toTypedVector(const Eigen::VectorXd& vec)
    {
        TypedArray<double> out =
            factory_.createArray<double>({static_cast<size_t>(vec.size()), 1});
        for (size_t i = 0; i < static_cast<size_t>(vec.size()); ++i) {
            out[i] = vec(i);
        }
        return out;
    }

    StructArray solutionToStruct(const DefaultSolution<double>& solution)
    {
        StructArray out = factory_.createStructArray({1, 1},
            {"x", "z", "s", "status", "obj_val", "obj_val_dual",
             "solve_time", "iterations", "r_prim", "r_dual"});

        out[0]["x"] = toTypedVector(solution.x);
        out[0]["z"] = toTypedVector(solution.z);
        out[0]["s"] = toTypedVector(solution.s);
        out[0]["status"] = factory_.createScalar(statusToString(solution.status));
        out[0]["obj_val"] = factory_.createScalar(solution.obj_val);
        out[0]["obj_val_dual"] = factory_.createScalar(solution.obj_val_dual);
        out[0]["solve_time"] = factory_.createScalar(solution.solve_time);
        out[0]["iterations"] =
            factory_.createScalar(static_cast<double>(solution.iterations));
        out[0]["r_prim"] = factory_.createScalar(solution.r_prim);
        out[0]["r_dual"] = factory_.createScalar(solution.r_dual);
        return out;
    }

public:
    void operator()(ArgumentList outputs, ArgumentList inputs) override
    {
        matlabPtr_ = getEngine();

        if (inputs.size() != 5 && inputs.size() != 6) {
            raiseError("clarabel_mex requires five or six inputs: P, q, A, b, cones [, settings].");
        }
        if (outputs.size() != 1) {
            raiseError("clarabel_mex returns a single solution struct.");
        }

        Eigen::SparseMatrix<double> P = toSparse(inputs[0], "P");
        Eigen::VectorXd q = toVector(inputs[1], "q");
        Eigen::SparseMatrix<double> A = toSparse(inputs[2], "A");
        Eigen::VectorXd b = toVector(inputs[3], "b");

        ConeSpec coneSpec = parseCones(inputs[4]);
        DefaultSettings<double> settings = inputs.size() == 6
            ? parseSettings(inputs[5])
            : mexDefaultSettings();

        DefaultSolver<double> solver(P, q, A, b, coneSpec.cones, settings);
        solver.print_to_buffer();
        solver.solve();
        if (settings.verbose) {
            std::cout << solver.get_print_buffer() << std::endl;
        }

        outputs[0] = solutionToStruct(solver.solution());
    }
};
