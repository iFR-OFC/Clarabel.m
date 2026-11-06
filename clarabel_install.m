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
%   Date: 06.11.2026
%
% ------------------------------------------------------------------------



%% Add paths

projectRoot = fileparts(mfilename('fullpath'));

if ispc
    disp('Windows detected')
    addpath(fullfile(projectRoot, "mex_files", "windows"))
elseif ismac
    disp('MacOS detected')
    addpath(fullfile(projectRoot, "mex_files", "mac_silicon"))
else
    disp('Linux detected')
    addpath(fullfile(projectRoot, "mex_files", "linux"))
end

addpath(fullfile(projectRoot, "DefaultCones"))
addpath(fullfile(projectRoot, "examples"))
addpath(projectRoot)

status = savepath;
if status ~= 0
    warning('Could not save the MATLAB path permanently. Check write permissions for pathdef.m.')
end
