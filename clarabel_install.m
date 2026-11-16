%% -----------------------------------------------------------------------
%
%   Short Description: A simple installation script for the Clarabel
%   .mex-interface. Depending on the User's OS the path to the
%   corresponding .mex file and libraries is set automatically.
%
%   Note that we make use of the pre-compiled .mex files here. If options
%   change, e.g., different compile options, the .mex files need to be
%   generated! 
%
%   Date: 16.11.2026
%
% ------------------------------------------------------------------------



% check for the correct OS
projectRoot = fileparts(mfilename('fullpath'));

if ispc
    disp('Windows detected')
    platform = 'windows';
elseif ismac
    disp('MacOS detected')
    platform = 'mac_silicon';
else
    disp('Linux detected')
    platform = 'linux';
end

% generate a mex_files folder
mexDir = fullfile(projectRoot, "mex_files");
if ~isfolder(mexDir)
    mkdir(mexDir)
end

% generate a folder depending on OS
platformDir = fullfile(mexDir, platform);
if ~isfolder(platformDir)
    mkdir(platformDir)
end

% download binaries from release page depending on OS
fprintf('Downloading binaries...');
zipFile = sprintf('%s.zip', platform);
zipPath = fullfile(platformDir, zipFile);
websave(zipPath, sprintf('https://github.com/iFR-OFC/Clarabel.m/releases/latest/download/%s.zip', platform))

% delete zip
unzip(zipPath, mexDir)
delete(zipPath)

% add path to mex files
addpath(platformDir)

% default cones
addpath(fullfile(projectRoot, "DefaultCones"))

% example folder
addpath(fullfile(projectRoot, "examples"))

addpath(projectRoot)

% check if the addpath was succesful
status = savepath;
if status ~= 0
    warning('Could not save the MATLAB path permanently. Check write permissions for pathdef.m.\n')
else
    fprintf('Clarabel was succesfully installed using the pre-compiled binaries!\n')
end
