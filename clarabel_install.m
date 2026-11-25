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
     
    % check what processor
     [~, cpuInfo] = system('powershell -Command "(Get-CimInstance Win32_Processor).Manufacturer"');

elseif ismac
    disp('MacOS detected')
    platform = 'mac_silicon';
    % check what processor
     [~, cpuInfo] = system('sysctl -n machdep.cpu.brand_string');
else
    disp('Linux detected')
    platform = 'linux';
    % check what processor
    [~, cpuInfo] = system('grep -m1 "vendor_id" /proc/cpuinfo');
end

% check what processor and download corresponding pre-compiled binary
% (currently only Windows!!!)
% Rational: For Mac we assume accelerate is used, which is the most
% efficient for Mac; Linux is a To Do!)
isIntel = contains(cpuInfo, 'Intel', 'IgnoreCase', true);
isAMD   = contains(cpuInfo, 'AMD', 'IgnoreCase', true);

if isIntel
    proc_prefix = 'Intel';
elseif isAMD
    proc_prefix = 'AMD';
else
    proc_prefix = '';
end

% generate a mex_files folder
mexDir = fullfile(projectRoot, "mex_files");
if ~isfolder(mexDir)
    mkdir(mexDir)
end

% generate a folder depending on OS
% (Windows distinguishes AMD/Intel, Mac and Linux do not)
if ispc
    if isempty(proc_prefix)
        error('clarabel_install:cpuNotDetected', ...
            'Could not detect an Intel or AMD CPU on Windows (detected: "%s").', strtrim(cpuInfo));
    end
    platformDir = fullfile(mexDir, [platform '_' proc_prefix]);
    zipFile = sprintf('%s_%s.zip', platform, proc_prefix);
    downloadUrl = sprintf('https://github.com/iFR-OFC/Clarabel.m/releases/latest/download/%s_%s.zip', platform, proc_prefix);
else
    platformDir = fullfile(mexDir, platform);
    zipFile = sprintf('%s.zip', platform);
    downloadUrl = sprintf('https://github.com/iFR-OFC/Clarabel.m/releases/latest/download/%s.zip', platform);
end

if ~isfolder(platformDir)
    mkdir(platformDir)
end

% download binaries from release page depending on OS
fprintf('Downloading binaries...');
zipPath = fullfile(platformDir, zipFile);
websave(zipPath, downloadUrl);


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
