function make_clarabel_mex(useFeatureSDP)
%MAKE_CLARABEL_MEX Compile clarabel_mex into mex_files/<OS> and update the MATLAB path.

    arguments
        useFeatureSDP (1,1) logical = true
    end

    projectRoot = fileparts(mfilename("fullpath"));

    if ispc
        osName = "Windows";   osFolder = "windows";
    elseif ismac
        if strcmp(computer("arch"), "maca64")
            osName = "macOS (Apple Silicon)"; osFolder = "mac_silicon";
        else
            osName = "macOS (Intel)";         osFolder = "mac_intel";
        end
    else
        osName = "Linux";     osFolder = "linux";
    end
    mexDir = fullfile(projectRoot, "mex_files", osFolder);
    if ~isfolder(mexDir)
        mkdir(mexDir);
    end

    eigenPath = getenv("EIGEN_PATH");
    if isempty(eigenPath) && ismac
        eigenPath = "/opt/homebrew/include/eigen3";
    end
    if ispc
        if isempty(eigenPath)
            eigenPath = "C:\vcpkg\installed\x64-windows\include\eigen3";
        elseif exist(fullfile(eigenPath, "Core"), "file") == 2
            eigenPath = fileparts(eigenPath);
        end
    end
    if isempty(eigenPath) || exist(eigenPath, "dir") ~= 7
        error("Eigen not found. Set the EIGEN_PATH environment variable.");
    end

    clarabelIncludePath = fullfile(projectRoot, "Clarabel", "include");
    releasePath = fullfile(projectRoot, "Clarabel", "rust_wrapper", "target", "release");
    if ~isfolder(clarabelIncludePath) || ~isfolder(releasePath)
        error("Clarabel include/library paths not found under: %s", ...
              fullfile(projectRoot, "Clarabel"));
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

    args = {'-outdir', mexDir, '-v', 'clarabel_mex.cpp'};
    if useFeatureSDP
        args = [args, {'-DFEATURE_SDP', '-DFEATURE_FAER_SPARSE'}];
    end
    args = [args, {sprintf('-I%s', eigenPath), sprintf('-I%s', clarabelIncludePath)}];
    if ispc
        args = [args, { ...
            fullfile(releasePath, "clarabel_c.dll.lib"), ...
            fullfile(releasePath, "clarabel_c.lib")}];
    else
        args = [args, {sprintf('-L%s', releasePath), '-lclarabel_c'}];
    end

    fprintf('OS              : %s\n', osName);
    fprintf('Eigen path      : %s\n', eigenPath);
    fprintf('Clarabel library: %s\n', fullfile(releasePath, libFile));
    fprintf('SDP/FAER feature: %d\n', useFeatureSDP);

    if ismac
        oldLdFlags = getenv("LDFLAGS");
        setenv("LDFLAGS", strtrim(sprintf('%s -Wl,-rpath,%s -Wl,-rpath,%s', ...
            oldLdFlags, releasePath, mexDir)));
        cleanupLdFlags = onCleanup(@() setenv("LDFLAGS", oldLdFlags));
    end

    mex(args{:});

    copyfile(fullfile(releasePath, libFile), mexDir);

    pathsToAdd = {mexDir, ...
                  fullfile(projectRoot, "examples"), ...
                  fullfile(projectRoot, "DefaultCones"), ...
                  projectRoot};
    addpath(pathsToAdd{:});

    fprintf('\nMEX build finished: %s\n', ...
            fullfile(mexDir, sprintf('clarabel_mex.%s', mexext)));
    fprintf('OS detected: %s\n', osName);
    fprintf('Added to the MATLAB path:\n');
    for p = pathsToAdd
        fprintf('  %s\n', p{1});
    end

    status = savepath;
    if status ~= 0
        warning('Could not save the MATLAB path permanently. Check write permissions for pathdef.m.')
    else
         fprintf('Clarabel Paths are set permanently');
    end

end
