// MATLAB C++ MEX interface for Clarabel solver
// Compatible with MATLAB R2025b on macOS ARM64

#include "mex.hpp"
#include "mexAdapter.hpp"
#include "MatlabDataArray.hpp"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include "clarabel.hpp"
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <codecvt>
#include <locale>

using namespace clarabel;
using matlab::mex::ArgumentList;
using matlab::data::ArrayFactory;
using matlab::data::TypedArray;
using matlab::data::StructArray;
using matlab::data::CharArray;

class MexFunction : public matlab::mex::Function {
private:
    std::shared_ptr<matlab::engine::MATLABEngine> matlabPtr;
    ArrayFactory factory;

    // Helper to convert u16string to string
    std::string u16toAscii(const std::u16string& u16str) {
        std::string result;
        for (char16_t c : u16str) {
            result += static_cast<char>(c);
        }
        return result;
    }

    // Convert MATLAB array to Eigen::SparseMatrix
    Eigen::SparseMatrix<double> matlabToSparseMatrix(const matlab::data::Array& arr) {
        auto dims = arr.getDimensions();
        size_t rows = dims[0];
        size_t cols = dims[1];
        
        if (arr.getType() == matlab::data::ArrayType::SPARSE_DOUBLE) {
            // Use MATLAB's find() function to extract sparse data safely
            std::vector<matlab::data::Array> args({arr});
            std::vector<matlab::data::Array> result = matlabPtr->feval(u"find", 3, args);
            
            TypedArray<double> i_arr = result[0];
            TypedArray<double> j_arr = result[1];
            TypedArray<double> v_arr = result[2];
            
            size_t nnz = i_arr.getNumberOfElements();
            
            Eigen::SparseMatrix<double> mat(rows, cols);
            std::vector<Eigen::Triplet<double>> triplets;
            triplets.reserve(nnz);
            
            for (size_t k = 0; k < nnz; ++k) {
                size_t i = static_cast<size_t>(i_arr[k]) - 1;  // MATLAB is 1-indexed
                size_t j = static_cast<size_t>(j_arr[k]) - 1;
                double v = v_arr[k];
                triplets.push_back(Eigen::Triplet<double>(i, j, v));
            }
            
            mat.setFromTriplets(triplets.begin(), triplets.end());
            mat.makeCompressed();
            return mat;
        } else {
            // Dense matrix - convert to sparse
            TypedArray<double> denseArr = arr;
            Eigen::MatrixXd dense(rows, cols);
            for (size_t i = 0; i < rows; i++) {
                for (size_t j = 0; j < cols; j++) {
                    dense(i, j) = denseArr[i][j];
                }
            }
            
            Eigen::SparseMatrix<double> sparse = dense.sparseView();
            sparse.makeCompressed();
            return sparse;
        }
    }

    // Convert MATLAB array to Eigen::VectorXd
    Eigen::VectorXd matlabToVector(const matlab::data::Array& arr) {
        TypedArray<double> typedArr = arr;
        auto dims = typedArr.getDimensions();
        size_t n = dims[0] * dims[1];
        
        Eigen::VectorXd vec(n);
        size_t idx = 0;
        for (auto elem : typedArr) {
            vec(idx++) = elem;
        }
        return vec;
    }

public:
    void operator()(ArgumentList outputs, ArgumentList inputs) override {
        matlabPtr = getEngine();
        
        // Check number of arguments
        checkArguments(outputs, inputs);
        
        // Extract inputs
        Eigen::SparseMatrix<double> P = matlabToSparseMatrix(inputs[0]);
        Eigen::VectorXd q = matlabToVector(inputs[1]);
        Eigen::SparseMatrix<double> A = matlabToSparseMatrix(inputs[2]);
        Eigen::VectorXd b = matlabToVector(inputs[3]);
        
        // Process cones
        std::vector<SupportedConeT<double>> cones = processCones(inputs[4]);
        
        // Process settings
        DefaultSettings<double> settings = processSettings(inputs[5]);
        
        // Build and solve
        DefaultSolver<double> solver(P, q, A, b, cones, settings);
        solver.print_to_buffer();
        solver.solve();
        if (settings.verbose) {
	    	std::string bufstr = solver.get_print_buffer();
		std::cout << bufstr << std::endl;
	    }
        
        // Get solution and create output
        DefaultSolution<double> solution = solver.solution();
        outputs[0] = createOutputStruct(solution);
    }

private:
    void checkArguments(ArgumentList outputs, ArgumentList inputs) {
        if (inputs.size() != 6) {
            matlabPtr->feval(u"error", 0, 
                std::vector<matlab::data::Array>({ factory.createScalar("Six input arguments required.") }));
        }
        if (outputs.size() != 1) {
            matlabPtr->feval(u"error", 0,
                std::vector<matlab::data::Array>({ factory.createScalar("One output required.") }));
        }
    }

    std::vector<SupportedConeT<double>> processCones(const matlab::data::Array& conesInput) {
        std::vector<SupportedConeT<double>> cones;
        
        // Check if input is a struct array
        if (conesInput.getType() != matlab::data::ArrayType::STRUCT) {
            matlabPtr->feval(u"error", 0,
                std::vector<matlab::data::Array>({ factory.createScalar("Cones input must be a struct array") }));
            return cones;
        }
        
        StructArray conesStruct = conesInput;
        
        for (size_t i = 0; i < conesStruct.getNumberOfElements(); i++) {
            CharArray coneNameArr = conesStruct[i]["coneName"];
            std::string coneNameStr = coneNameArr.toAscii();
            
            if (coneNameStr == "ZeroCone") {
                TypedArray<double> dimArr = conesStruct[i]["dim"];
                double dimVal = dimArr[0];
                size_t dim = static_cast<size_t>(dimVal);
                if (dim > 0) {
                    cones.push_back(ZeroConeT<double>(dim));
                }
            } else if (coneNameStr == "NonnegativeCone") {
                TypedArray<double> dimArr = conesStruct[i]["dim"];
                double dimVal = dimArr[0];
                size_t dim = static_cast<size_t>(dimVal);
                if (dim > 0) {
                    cones.push_back(NonnegativeConeT<double>(dim));
                }
            } else if (coneNameStr == "SecondOrderCone") {
                TypedArray<double> dimArr = conesStruct[i]["dim"];
                double dimVal = dimArr[0];
                size_t dim = static_cast<size_t>(dimVal);
                if (dim > 0) {
                    cones.push_back(SecondOrderConeT<double>(dim));
                }
            } else if (coneNameStr == "ExponentialCone") {
                cones.push_back(ExponentialConeT<double>());
            } else if (coneNameStr == "PowerCone") {
                TypedArray<double> dimArr = conesStruct[i]["dim"];
                double power = dimArr[0];
                cones.push_back(PowerConeT<double>(power));
            } else if (coneNameStr == "GenPowerCone") {
                TypedArray<double> dimArr = conesStruct[i]["dim"];
                double dimVal = dimArr[0];
                size_t dim = static_cast<size_t>(dimVal);
                Eigen::VectorXd alpha = matlabToVector(conesStruct[i]["alpha"]);
                cones.push_back(GenPowerConeT<double>(alpha, dim));
            }
#ifdef FEATURE_SDP
            else if (coneNameStr == "PSDTriangleCone") {
                TypedArray<double> dimArr = conesStruct[i]["dim"];
                double dimVal = dimArr[0];
                size_t dim = static_cast<size_t>(dimVal);
                if (dim > 0) {
                    cones.push_back(PSDTriangleConeT<double>(dim));
                }
            }
#else
            else if (coneNameStr == "PSDTriangleCone") {
                matlabPtr->feval(u"error", 0,
                    std::vector<matlab::data::Array>({ factory.createScalar(
                        "SDP cones are not supported in this build.") }));
            }
#endif
            else {
                matlabPtr->feval(u"error", 0,
                    std::vector<matlab::data::Array>({ factory.createScalar(
                        "Unknown cone name: " + coneNameStr) }));
            }
        }
        
        return cones;
    }

    template<typename T>
    T getField(const StructArray& s, size_t idx, const char* field) {
        matlab::data::Array fieldArr = s[idx][field];
        
        // Handle different types
        if (fieldArr.getType() == matlab::data::ArrayType::UINT32) {
            TypedArray<uint32_t> arr = fieldArr;
            return static_cast<T>(arr[0]);
        } else if (fieldArr.getType() == matlab::data::ArrayType::DOUBLE) {
            TypedArray<double> arr = fieldArr;
            return static_cast<T>(arr[0]);
        } else if (fieldArr.getType() == matlab::data::ArrayType::LOGICAL) {
            TypedArray<bool> arr = fieldArr;
            return static_cast<T>(arr[0]);
        }
        
        // Fallback
        TypedArray<T> arr = fieldArr;
        return arr[0];
    }

    DefaultSettings<double> processSettings(const matlab::data::Array& settingsInput) {
        StructArray settingsStruct = settingsInput;
        
        // Extract all settings fields using helper
        bool verbose = getField<bool>(settingsStruct, 0, "verbose");
        int max_iter = static_cast<int>(getField<double>(settingsStruct, 0, "max_iter"));
        int equilibrate_max_iter = static_cast<int>(getField<double>(settingsStruct, 0, "equilibrate_max_iter"));
        int iterative_refinement_max_iter = static_cast<int>(getField<double>(settingsStruct, 0, "iterative_refinement_max_iter"));
        
        bool equilibrate_enable = getField<bool>(settingsStruct, 0, "equilibrate_enable");
        bool direct_kkt_solver = getField<bool>(settingsStruct, 0, "direct_kkt_solver");
        bool static_regularization_enable = getField<bool>(settingsStruct, 0, "static_regularization_enable");
        bool dynamic_regularization_enable = getField<bool>(settingsStruct, 0, "dynamic_regularization_enable");
        bool iterative_refinement_enable = getField<bool>(settingsStruct, 0, "iterative_refinement_enable");
        bool presolve_enable = getField<bool>(settingsStruct, 0, "presolve_enable");
        
        double time_limit = getField<double>(settingsStruct, 0, "time_limit");
        double max_step_fraction = getField<double>(settingsStruct, 0, "max_step_fraction");
        double tol_gap_abs = getField<double>(settingsStruct, 0, "tol_gap_abs");
        double tol_gap_rel = getField<double>(settingsStruct, 0, "tol_gap_rel");
        double tol_feas = getField<double>(settingsStruct, 0, "tol_feas");
        double tol_infeas_abs = getField<double>(settingsStruct, 0, "tol_infeas_abs");
        double tol_infeas_rel = getField<double>(settingsStruct, 0, "tol_infeas_rel");
        double tol_ktratio = getField<double>(settingsStruct, 0, "tol_ktratio");
        double reduced_tol_gap_abs = getField<double>(settingsStruct, 0, "reduced_tol_gap_abs");
        double reduced_tol_gap_rel = getField<double>(settingsStruct, 0, "reduced_tol_gap_rel");
        double reduced_tol_feas = getField<double>(settingsStruct, 0, "reduced_tol_feas");
        double reduced_tol_infeas_abs = getField<double>(settingsStruct, 0, "reduced_tol_infeas_abs");
        double reduced_tol_infeas_rel = getField<double>(settingsStruct, 0, "reduced_tol_infeas_rel");
        double reduced_tol_ktratio = getField<double>(settingsStruct, 0, "reduced_tol_ktratio");
        double equilibrate_min_scaling = getField<double>(settingsStruct, 0, "equilibrate_min_scaling");
        double equilibrate_max_scaling = getField<double>(settingsStruct, 0, "equilibrate_max_scaling");
        double linesearch_backtrack_step = getField<double>(settingsStruct, 0, "linesearch_backtrack_step");
        double min_switch_step_length = getField<double>(settingsStruct, 0, "min_switch_step_length");
        double min_terminate_step_length = getField<double>(settingsStruct, 0, "min_terminate_step_length");
        double static_regularization_constant = getField<double>(settingsStruct, 0, "static_regularization_constant");
        double static_regularization_proportional = getField<double>(settingsStruct, 0, "static_regularization_proportional");
        double dynamic_regularization_eps = getField<double>(settingsStruct, 0, "dynamic_regularization_eps");
        double dynamic_regularization_delta = getField<double>(settingsStruct, 0, "dynamic_regularization_delta");
        double iterative_refinement_reltol = getField<double>(settingsStruct, 0, "iterative_refinement_reltol");
        double iterative_refinement_abstol = getField<double>(settingsStruct, 0, "iterative_refinement_abstol");
        double iterative_refinement_stop_ratio = getField<double>(settingsStruct, 0, "iterative_refinement_stop_ratio");
        
        // Build settings
        DefaultSettings<double> settings = DefaultSettingsBuilder<double>::default_settings()
            .verbose(verbose)
            .max_iter(max_iter)
            .equilibrate_max_iter(equilibrate_max_iter)
            .iterative_refinement_max_iter(iterative_refinement_max_iter)
            .equilibrate_enable(equilibrate_enable)
            .direct_kkt_solver(direct_kkt_solver)
            .static_regularization_enable(static_regularization_enable)
            .dynamic_regularization_enable(dynamic_regularization_enable)
            .iterative_refinement_enable(iterative_refinement_enable)
            .presolve_enable(presolve_enable)
            .time_limit(time_limit)
            .max_step_fraction(max_step_fraction)
            .tol_gap_abs(tol_gap_abs)
            .tol_gap_rel(tol_gap_rel)
            .tol_feas(tol_feas)
            .tol_infeas_abs(tol_infeas_abs)
            .tol_infeas_rel(tol_infeas_rel)
            .tol_ktratio(tol_ktratio)
            .reduced_tol_gap_abs(reduced_tol_gap_abs)
            .reduced_tol_gap_rel(reduced_tol_gap_rel)
            .reduced_tol_feas(reduced_tol_feas)
            .reduced_tol_infeas_abs(reduced_tol_infeas_abs)
            .reduced_tol_infeas_rel(reduced_tol_infeas_rel)
            .reduced_tol_ktratio(reduced_tol_ktratio)
            .equilibrate_min_scaling(equilibrate_min_scaling)
            .equilibrate_max_scaling(equilibrate_max_scaling)
            .linesearch_backtrack_step(linesearch_backtrack_step)
            .min_switch_step_length(min_switch_step_length)
            .min_terminate_step_length(min_terminate_step_length)
            .static_regularization_constant(static_regularization_constant)
            .static_regularization_proportional(static_regularization_proportional)
            .dynamic_regularization_eps(dynamic_regularization_eps)
            .dynamic_regularization_delta(dynamic_regularization_delta)
            .iterative_refinement_reltol(iterative_refinement_reltol)
            .iterative_refinement_abstol(iterative_refinement_abstol)
            .iterative_refinement_stop_ratio(iterative_refinement_stop_ratio)
            .build();
        
        // Process solve method
        CharArray solveMethodArr = settingsStruct[0]["direct_solve_method"];
        std::string solveMethod = solveMethodArr.toAscii();
        if (solveMethod == "qdldl") {
            settings.direct_solve_method = ClarabelDirectSolveMethods::QDLDL;
        } else if (solveMethod == "FAER") {
            settings.direct_solve_method = ClarabelDirectSolveMethods::FAER;
        }
        
        // Process merge method
        CharArray mergeMethodArr = settingsStruct[0]["chordal_decomposition_merge_method"];
        std::string mergeMethod = mergeMethodArr.toAscii();
        if (mergeMethod == "CLIQUE_GRAPH") {
            settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::CLIQUE_GRAPH;
        } else if (mergeMethod == "PARENT_CHILD") {
            settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::PARENT_CHILD;
        } else if (mergeMethod == "NONE") {
            settings.chordal_decomposition_merge_method = ClarabelCliqueMergeMethods::NONE;
        }
        
        return settings;
    }

    matlab::data::StructArray createOutputStruct(const DefaultSolution<double>& solution) {
        StructArray output = factory.createStructArray({1, 1}, 
            {"x", "z", "s", "status", "obj_val", "obj_val_dual", "solve_time", "iterations", "r_prim", "r_dual"});
        
        // Create x vector
        size_t x_size = static_cast<size_t>(solution.x.size());
        TypedArray<double> x = factory.createArray<double>({x_size, 1});
        for (size_t i = 0; i < x_size; i++) {
            x[i] = solution.x(i);
        }
        output[0]["x"] = x;
        
        // Create z vector
        size_t z_size = static_cast<size_t>(solution.z.size());
        TypedArray<double> z = factory.createArray<double>({z_size, 1});
        for (size_t i = 0; i < z_size; i++) {
            z[i] = solution.z(i);
        }
        output[0]["z"] = z;
        
        // Create s vector
        size_t s_size = static_cast<size_t>(solution.s.size());
        TypedArray<double> s = factory.createArray<double>({s_size, 1});
        for (size_t i = 0; i < s_size; i++) {
            s[i] = solution.s(i);
        }
        output[0]["s"] = s;
        
        // Status string
        std::string status_str;
        switch (solution.status) {
            case SolverStatus::Unsolved: status_str = "Unsolved"; break;
            case SolverStatus::Solved: status_str = "Solved"; break;
            case SolverStatus::PrimalInfeasible: status_str = "PrimalInfeasible"; break;
            case SolverStatus::DualInfeasible: status_str = "DualInfeasible"; break;
            case SolverStatus::AlmostSolved: status_str = "AlmostSolved"; break;
            case SolverStatus::AlmostPrimalInfeasible: status_str = "AlmostPrimalInfeasible"; break;
            case SolverStatus::AlmostDualInfeasible: status_str = "AlmostDualInfeasible"; break;
            case SolverStatus::MaxIterations: status_str = "MaxIterations"; break;
            case SolverStatus::MaxTime: status_str = "MaxTime"; break;
            case SolverStatus::NumericalError: status_str = "NumericalError"; break;
            case SolverStatus::InsufficientProgress: status_str = "InsufficientProgress"; break;
            default: status_str = "Unknown"; break;
        }
        output[0]["status"] = factory.createScalar(status_str);
        
        // Scalars
        output[0]["obj_val"] = factory.createScalar(solution.obj_val);
        output[0]["obj_val_dual"] = factory.createScalar(solution.obj_val_dual);
        output[0]["solve_time"] = factory.createScalar(solution.solve_time);
        output[0]["iterations"] = factory.createScalar(static_cast<double>(solution.iterations));
        output[0]["r_prim"] = factory.createScalar(solution.r_prim);
        output[0]["r_dual"] = factory.createScalar(solution.r_dual);
        
        return output;
    }
};
