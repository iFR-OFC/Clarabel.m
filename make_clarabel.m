clc
clear

% Define the current folder as the destination
destinationFolder = pwd;

% Define the source path of the file
if ispc
    % Windows paths
    sourceFolder = 'Clarabel\rust_wrapper\target\release';  % Replace with the actual path
    fileName = 'clarabel_c.dll';                            % Windows shared library
elseif ismac
    sourceFolder = 'Clarabel/rust_wrapper/target/release';  % macOS path with forward slashes
    fileName = 'libclarabel_c.dylib';                        % macOS shared library‚
elseif isunix
    % Unix-based (Linux) paths
    sourceFolder = 'Clarabel/rust_wrapper/target/release';  % Unix path with forward slashes
    fileName = 'libclarabel_c.so';                          % Unix shared library (likely .so)
else
    error('Unsupported operating system');
end

% Create the full path for the source file and the destination
sourceFile = fullfile(sourceFolder, fileName);
destinationFile = fullfile(destinationFolder, fileName);

% Copy the file
copyfile(sourceFile, destinationFile);

% Display a success message
disp(['File copied to: ', destinationFile]);

% just for display output
% pause(1)
disp('');
disp('');

% Set to true to include -DFEATURE_SDP if you want to use the SDP feature
% IMPORTANT: libraries must be compiled with the sdp-feature!
useFeatureSDP = true;

% Get eigen path from environment variable or change the folder where you
% stored eigen
if ispc
    eigenPath = getenv('EIGEN_PATH');

elseif isunix && ~ismac
    eigenPath = getenv('EIGEN_PATH');

else
    eigenPath = '/opt/homebrew/include/eigen3';  % Apple Silicon

end

% If environment variable is not set, allow the user to manually input the path
if isempty(eigenPath)
    warning('Environment variable EIGEN_PATH is not set.');
    % Set the path where you saved again manually
    warning('Use manually inserted path.')
end

% Check if the path is valid (you could also check if a specific file exists here)
if exist(eigenPath, 'dir') ~= 7
    error(['The specified Eigen path is invalid: ', eigenPath]);
else
    disp(['Eigen path is: ', eigenPath]);
end

% just for display output
disp('');
disp('');

% shortly wait
pause(0.5)

% Common paths to clarabel; make sure you first compiled the libraries!
if ispc
    clarabelIncludePath = 'Clarabel\include';
    clarabelDLLPath = 'Clarabel\rust_wrapper\target\release\clarabel_c.dll.lib';
    clarabelLibPath = 'Clarabel\rust_wrapper\target\release\clarabel_c.lib';


    % Construct the mex command
    mexCmd = [
        'mex -v clarabel_mex.cpp ' ...
        (useFeatureSDP * '-DFEATURE_SDP ') ...          % Conditionally add -DFEATURE_SDP
        (useFeatureSDP * '-DFEATURE_FAER_SPARSE ') ...  % Conditionally add -DFEATURE_FAER_SPARSE
        '-I"' eigenPath '" ' ...
        '-I"' clarabelIncludePath '" ' ...
        '"' clarabelDLLPath '" ' ...
        '"' clarabelLibPath '"'
        ];
elseif ismac

    % Paths
    clarabelIncludePath = fullfile(pwd, 'Clarabel', 'include');  % absolute path
    clarabelRustWrapperPath = fullfile(pwd, 'Clarabel', 'rust_wrapper', 'target', 'release');


    % Conditional flags
    featureFlags = '';
    if exist('useFeatureSDP','var') && useFeatureSDP
        featureFlags = '-DFEATURE_SDP -DFEATURE_FAER_SPARSE';
    end

    % Build MEX file using new C++ MEX API (compatible with R2025b)
    mexCmd = sprintf([ ...
        'mex -v ' ...
        'clarabel_mex.cpp ' ...
        '%s ' ...                         % conditional flags
        '-I"%s" ' ...                      % Eigen
        '-I"%s" ' ...                      % Clarabel include
        '-L"%s" -lclarabel_c '  ...        % Rust library
        ], featureFlags, eigenPath, clarabelIncludePath, clarabelRustWrapperPath);

elseif isunix
    clarabelIncludePath = 'Clarabel/include';
    clarabelLibPath = 'Clarabel/rust_wrapper/target/raarch64-apple-darwinlibclarabel_c.so';  % Change .dll to .so for Linux
    clarabelRustWrapperPath = 'Clarabel/rust_wrapper/target/aarch64-apple-darwin';           % Path to the directory containing the shared library

    % Construct the mex command
    mexCmd = [
        'mex -v clarabel_mex.cpp ' ...
        (useFeatureSDP * '-DFEATURE_SDP ') ...          % Conditionally add -DFEATURE_SDP
        (useFeatureSDP * '-DFEATURE_FAER_SPARSE ') ...  % Conditionally add -DFEATURE_FAER_SPARSE
        '-I"' eigenPath '" ' ...
        '-I"' clarabelIncludePath '" ' ...
        '-L"' clarabelRustWrapperPath '" ' ...          % Specify library path with -L
        '-lclarabel_c'                                  % Link against the shared library (no "lib" prefix and no ".so" suffix)
        ];
else
    error('Unsupported operating system');
end


%% Execute the mex command
disp('Executing mex command:');
disp(mexCmd);
eval(mexCmd); % Run the constructed mex command

disp('');
disp('');

pause(1)


%% add Clarabel_matlab path and subfolder permanently to matlab path
disp('Adding clarabel_matlab folder and subfolder permanently to your MATLAB path.');
folderToAdd = pwd;
addpath(genpath(folderToAdd));
savepath;


disp('Clarabel .mex generation done.');

%% --- helper function ---
function s = conditionalFlag(cond,flag)
if cond
    s = flag;
else
    s = '';
end
end