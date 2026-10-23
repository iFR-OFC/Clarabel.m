function make_clarabel_mex(useFeatureSDP)
%MAKE_CLARABEL_MEX Compile the Clarabel MEX interface.
%
%   make_clarabel_mex()                compile clarabel_mex (default)
%   make_clarabel_mex(useFeatureSDP)   compile clarabel_mex; pass false
%                                      to build without SDP/FAER
%
%   The MEX provides the one-shot solver
%       sol = clarabel_mex(P, q, A, b, cones, settings)
%
%   PREREQUISITES
%     * Clarabel.cpp checked out in <this folder>/Clarabel with the Rust
%       library built in Clarabel/rust_wrapper/target/release (see README).
%     * Eigen 3.4+. The path is taken from the EIGEN_PATH environment
%       variable, falling back to /opt/homebrew/include/eigen3
%       (macOS with Homebrew).
%
%   NOTES
%     * useFeatureSDP must match the features the Rust library was built
%       with; it adds -DFEATURE_SDP -DFEATURE_FAER_SPARSE to the MEX build.
%     * macOS: the MEX gets an rpath to the Clarabel library folder.
%     * Windows/Linux: the Clarabel shared library is copied next to the
%       MEX file so it can be found at runtime.
%
%   See also: clarabel_mex.cpp

    arguments
        useFeatureSDP (1,1) logical = true
    end

    projectRoot = fileparts(mfilename("fullpath"));

    % --- resolve paths ---------------------------------------------------
    eigenPath = getenv("EIGEN_PATH");
    if isempty(eigenPath) && ismac
        eigenPath = "/opt/homebrew/include/eigen3";
    end
    if isempty(eigenPath) || exist(eigenPath, "dir") ~= 7
        error("Eigen not found. Set the EIGEN_PATH environment variable.");
    end

    clarabelIncludePath = fullfile(projectRoot, "Clarabel", "include");
    releasePath = fullfile(projectRoot, "Clarabel", "rust_wrapper", "target", "release");
    if ~isfolder(clarabelIncludePath) || ~isfolder(releasePath)
        error(["Clarabel include/library paths not found under:", ...
               fullfile(projectRoot, "Clarabel")]);
    end

    if ispc
        libFile = "clarabel_c.dll";
    elseif ismac
        libFile = "libclarabel_c.dylib";
    else
        libFile = "libclarabel_c.so";
    end
    if ~isfile(fullfile(releasePath, libFile))
        error("Clarabel library not found: %s", fullfile(releasePath, libFile));
    end

    % --- assemble mex arguments ------------------------------------------
    args = {'-outdir', projectRoot, '-v', 'clarabel_mex.cpp'};

    if useFeatureSDP
        args = [args, {'-DFEATURE_SDP', '-DFEATURE_FAER_SPARSE'}];
    end

    args = [args, {sprintf('-I%s', eigenPath), sprintf('-I%s', clarabelIncludePath)}];

    if ispc
        % MSVC-style linking against the Rust import libraries.
        args = [args, { ...
            fullfile(releasePath, "clarabel_c.dll.lib"), ...
            fullfile(releasePath, "clarabel_c.lib")}];
    else
        args = [args, {sprintf('-L%s', releasePath), '-lclarabel_c'}];
    end

    % --- build ------------------------------------------------------------
    fprintf('Target          : clarabel_mex\n');
    fprintf('Eigen path      : %s\n', eigenPath);
    fprintf('Clarabel include: %s\n', clarabelIncludePath);
    fprintf('Clarabel library: %s\n', fullfile(releasePath, libFile));
    fprintf('SDP/FAER feature: %d\n', useFeatureSDP);

    cleanupLdFlags = [];
    if ismac
        % Give the MEX an rpath to the Clarabel library folder. Restore the
        % user's LDFLAGS afterwards.
        oldLdFlags = getenv("LDFLAGS");
        setenv("LDFLAGS", strtrim(sprintf('%s -Wl,-rpath,%s', oldLdFlags, releasePath)));
        cleanupLdFlags = onCleanup(@() setenv("LDFLAGS", oldLdFlags));
    end

    mex(args{:});

    % --- runtime library placement (Windows/Linux) ------------------------
    if ~ismac
        copyfile(fullfile(releasePath, libFile), projectRoot);
    end

    fprintf('\nMEX build finished: %s\n', ...
            fullfile(projectRoot, sprintf('clarabel_mex.%s', mexext)));

end
