// clarabel_mex.cpp
//
// MATLAB C++ MEX interface for the Clarabel interior-point solver.
//
// Usage:
//   solution = clarabel_mex(P, q, A, b, cones, settings)
//
// Inputs:
//   P        sparse or dense n x n matrix   (quadratic cost: 1/2 x'Px + q'x)
//   q        n x 1 vector
//   A        sparse or dense m x n matrix  (constraints: Ax + s = b, s in K)
//   b        m x 1 vector
//   cones    struct array built by the functions in DefaultCones/
//            (zeroConeT, NonnegativeConeT, SecondOrderConeT, exponentialConeT,
//             PowerConeT, GenPowerConeT, PSDTriangleConeT)
//   settings struct produced by DefaultSettings()
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

    bool readBool(const StructArray& s, const char* field)
    {
        return readScalar(s[0][field], field) != 0.0;
    }

    uint32_t readUint(const StructArray& s, const char* field)
    {
        double value = readScalar(s[0][field], field);
        if (value < 0.0) {
            raiseError(std::string(field) + " must be non-negative.");
        }
        return static_cast<uint32_t>(value);
    }

    double readDouble(const StructArray& s, const char* field)
    {
        return readScalar(s[0][field], field);
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

    DefaultSettings<double> parseSettings(const Array& input)
    {
        StructArray s = input;

        // Start from the library defaults, then overwrite with the values
        // supplied in the settings struct (produced by DefaultSettings.m).
        DefaultSettings<double> settings = DefaultSettings<double>::default_settings();

        // Basic
        settings.verbose     = readBool(s, "verbose");
        settings.max_iter    = readUint(s, "max_iter");
        settings.time_limit  = readDouble(s, "time_limit");
        settings.presolve_enable = readBool(s, "presolve_enable");

        // Scaling / equilibration
        settings.equilibrate_enable    = readBool(s, "equilibrate_enable");
        settings.equilibrate_max_iter  = readUint(s, "equilibrate_max_iter");
        settings.equilibrate_min_scaling = readDouble(s, "equilibrate_min_scaling");
        settings.equilibrate_max_scaling = readDouble(s, "equilibrate_max_scaling");

        // Termination tolerances
        settings.tol_gap_abs   = readDouble(s, "tol_gap_abs");
        settings.tol_gap_rel   = readDouble(s, "tol_gap_rel");
        settings.tol_feas      = readDouble(s, "tol_feas");
        settings.tol_infeas_abs = readDouble(s, "tol_infeas_abs");
        settings.tol_infeas_rel = readDouble(s, "tol_infeas_rel");
        settings.tol_ktratio   = readDouble(s, "tol_ktratio");
        settings.reduced_tol_gap_abs   = readDouble(s, "reduced_tol_gap_abs");
        settings.reduced_tol_gap_rel   = readDouble(s, "reduced_tol_gap_rel");
        settings.reduced_tol_feas     = readDouble(s, "reduced_tol_feas");
        settings.reduced_tol_infeas_abs = readDouble(s, "reduced_tol_infeas_abs");
        settings.reduced_tol_infeas_rel = readDouble(s, "reduced_tol_infeas_rel");
        settings.reduced_tol_ktratio   = readDouble(s, "reduced_tol_ktratio");

        // Step size / linesearch
        settings.max_step_fraction          = readDouble(s, "max_step_fraction");
        settings.linesearch_backtrack_step  = readDouble(s, "linesearch_backtrack_step");
        settings.min_switch_step_length    = readDouble(s, "min_switch_step_length");
        settings.min_terminate_step_length  = readDouble(s, "min_terminate_step_length");

        // KKT solver
        settings.direct_kkt_solver = readBool(s, "direct_kkt_solver");
        std::string solveMethod = getString(s[0]["direct_solve_method"],
                                            "direct_solve_method");
        if (solveMethod == "qdldl") {
            settings.direct_solve_method = ClarabelDirectSolveMethods::QDLDL;
#ifdef FEATURE_FAER_SPARSE
        } else if (solveMethod == "FAER") {
            settings.direct_solve_method = ClarabelDirectSolveMethods::FAER;
#else
        } else if (solveMethod == "FAER") {
            raiseWarning("FAER requested, but this MEX was built without FAER support. Using QDLDL.");
            settings.direct_solve_method = ClarabelDirectSolveMethods::QDLDL;
#endif
        } else {
            raiseError("Unknown direct_solve_method: " + solveMethod);
        }

        // Static/dynamic regularization
        settings.static_regularization_enable      = readBool(s, "static_regularization_enable");
        settings.static_regularization_constant     = readDouble(s, "static_regularization_constant");
        settings.static_regularization_proportional = readDouble(s, "static_regularization_proportional");
        settings.dynamic_regularization_enable     = readBool(s, "dynamic_regularization_enable");
        settings.dynamic_regularization_eps        = readDouble(s, "dynamic_regularization_eps");
        settings.dynamic_regularization_delta      = readDouble(s, "dynamic_regularization_delta");

        // Iterative refinement
        settings.iterative_refinement_enable     = readBool(s, "iterative_refinement_enable");
        settings.iterative_refinement_max_iter  = readUint(s, "iterative_refinement_max_iter");
        settings.iterative_refinement_reltol    = readDouble(s, "iterative_refinement_reltol");
        settings.iterative_refinement_abstol    = readDouble(s, "iterative_refinement_abstol");
        settings.iterative_refinement_stop_ratio = readDouble(s, "iterative_refinement_stop_ratio");

#ifdef FEATURE_SDP
        // Chordal decomposition (SDP problems only)
        settings.chordal_decomposition_enable       = readBool(s, "chordal_decomposition_enable");
        settings.chordal_decomposition_compact      = readBool(s, "chordal_decomposition_compact");
        settings.chordal_decomposition_complete_dual = readBool(s, "chordal_decomposition_complete_dual");

        std::string mergeMethod = getString(s[0]["chordal_decomposition_merge_method"],
                                            "chordal_decomposition_merge_method");
        if (mergeMethod == "CLIQUE_GRAPH") {
            settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::CLIQUE_GRAPH;
        } else if (mergeMethod == "PARENT_CHILD") {
            settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::PARENT_CHILD;
        } else if (mergeMethod == "NONE") {
            settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::NONE;
        } else {
            raiseError("Unknown chordal_decomposition_merge_method: " + mergeMethod);
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

        if (inputs.size() != 6) {
            raiseError("clarabel_mex requires six inputs: P, q, A, b, cones, settings.");
        }
        if (outputs.size() != 1) {
            raiseError("clarabel_mex returns a single solution struct.");
        }

        Eigen::SparseMatrix<double> P = toSparse(inputs[0], "P");
        Eigen::VectorXd q = toVector(inputs[1], "q");
        Eigen::SparseMatrix<double> A = toSparse(inputs[2], "A");
        Eigen::VectorXd b = toVector(inputs[3], "b");

        ConeSpec coneSpec = parseCones(inputs[4]);
        DefaultSettings<double> settings = parseSettings(inputs[5]);

        DefaultSolver<double> solver(P, q, A, b, coneSpec.cones, settings);
        solver.print_to_buffer();
        solver.solve();
        if (settings.verbose) {
            std::cout << solver.get_print_buffer() << std::endl;
        }

        outputs[0] = solutionToStruct(solver.solution());
    }
};
