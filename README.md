# clarabel.m

MATLAB mex interface to [Clarabel](https://clarabel.org/stable/), an interior point solver for convex optimization.

# Description

This repository contains a (work in progress) MATLAB mex interface to the recently introduced interior point solver [Clarabel](https://clarabel.org/stable/). The mex interface is interfaced to 
[Clarabel.cpp](https://github.com/oxfordcontrol/Clarabel.cpp/tree/main), the C/C++ interface to the Rust implementation of Clarabel. The interface to the C++ implementation is realized via [eigen](https://eigen.tuxfamily.org/index.php?title=Main\_Page](https://eigen.tuxfamily.org/index.php?title=Main_Page)).

# Installation
The following instructions are for Windows and Linux. On Windows we used an MS Visual Studio compiler. The .mex interface uses activated SDP feature of clarabel and assume we also use the more efficient [faer-rs](https://github.com/sarah-quinones/faer-rs) library.

1. Install or update [Rust](https://www.rust-lang.org/tools/install) (if necessary)
2. Install [eigen](https://eigen.tuxfamily.org/index.php?title=Main\_Page](https://eigen.tuxfamily.org/index.php?title=Main_Page)). Add it to your ENVIRONMENT VARIABLES (or later passe the eigen folder in make_clarabel.m) 
> **Note**: 
> On Linux, the standard installation procedure using `sudo apt-get install libeigen3-dev` will not work because the version available (v3.3.7) is outdated, version v3.4 is required. To resolve this, you will need to manually install the latest version of eigen directly from the [eigen website](https://eigen.tuxfamily.org/dox/). Follow the instructions there for a successful installation.
3. Make sure you have a C/C++ compiler (in the following it is assumed that Visual Studio compiler is available; needed to compile the Rust-Wrapper and the actual .mex function)
4. Clone this repository
```
git clone https://github.com/iFR-ACSO/Clarabel.m.git 
```

5. Clone [Clarabel.cpp](https://github.com/oxfordcontrol/Clarabel.cpp/tree/main) it into the main folder of Clarabel.m; name it Clarabel (both needed because the relative paths are set in make_clarabel_mex.m)
```
git clone git@github.com:oxfordcontrol/Clarabel.cpp.git Clarabel
```
6. Go to the Clarabel folder
7. Get the submodule (Rust implementation of the actual solver)
```
git submodule update --init --recursive
```
8. Adapt the Clarabel/Cmakelist.txt with the features you want. To use SDPs on Windows, select MKL (seems to be the only working solution on Windows)
   ```
   ...
   set(CLARABEL_FEATURE_SDP "sdp-mkl" CACHE STRING "Package for SDP to be selected")
   ...
   ```
9. In the **Clarabel** folder, open a terminal (Linux) or **Windows PowerShell** (Windows).
10.  Use the following commands to compile in release mode with faer-rs (efficient linear algebra in Rust) and vcpkg (on Windows) (change **PathToVcpk** to your installation directory)
   On Windows:
   ```powershell
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=PathToVcpk/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_BUILD_TYPE=Release -DCLARABEL_FEATURE_FAER_SPARSE=true
   cmake --build .
   ```
   On Linux:
   ```bash
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=PathToVcpk/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-linux -DCMAKE_BUILD_TYPE=Release -DCLARABEL_FEATURE_FAER_SPARSE=true
   cmake --build .
   ```

11.  Open Matlab and run
```
make_clarabel_mex.m
```
> **Note**: 
> To successfully run `make_clarabel_mex.m`, you need to set the path to the Eigen library in the `EIGEN_PATH` environment variable if it is not found automatically (on macOS with Homebrew, `/opt/homebrew/include/eigen3` is used as the default). The MEX can be built with or without the SDP and faer features by calling `make_clarabel_mex(useFeatureSDP)`. Make sure the feature selection matches the features the Clarabel Rust library was compiled with.

This command compiles the mex file `clarabel_mex`. After compilation, the path to Clarabel.m is added permanently to your Matlab path. This includes clarabel_mex, DefaultSettings.m, the folder DefaultCones with the specific cone functions and the example folder, i.e., the re-implemented examples in Matlab from the C++ example package.


# Usage Example

Below is an usage example to showcase solving an optimization problem.
The following example simply implements the [getting started example for Python](https://clarabel.org/stable/python/getting_started_py/) via the MATLAB/mex interface. More Examples can be found in the examples folder.

```matlab

% define cost: 1/2 x^T P x + q^T x
P = sparse(triu([ 3., 1., -1.;
                  1., 4.,  2.;
                 -1., 2.,  5.]));


q = [1., 2., -3.];

% define constraints
Aeq = sparse([1 1 -1]);
beq = 1;

Aineq = sparse([0 1 0; 0 0 1]);
bineq = [2;2];


Asoc = -speye(3);
bsoc = zeros(3,1);

% stack constraints
A = [Aeq;Aineq;Asoc];

b = [beq;bineq;bsoc];

% stack cones
cones = [zeroConeT(1);
         NonnegativeConeT(2);
         SecondOrderConeT(3)];

% generate settings
settings = DefaultSettings();

settings.verbose = false;
settings.time_limit = 5.;

% call interface
sol = clarabel_mex(P,q,A,b,cones,settings)

```

The order of the cones can be arbitrary similar to the Python interface.
