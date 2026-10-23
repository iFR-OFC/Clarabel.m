clc
clear


n = 3;


% define cost
P = sparse(eye(6));
q = [1., 0., 1., 0., 0., 1.];

% define constraints
sqrt2 = sqrt(2);
A = sparse([-1.,     0.,  0.,     0.,     0.,  0.;
             0., -sqrt2,  0.,     0.,     0.,  0.;
             0.,     0., -1.,     0.,     0.,  0.;
             0.,     0.,  0., -sqrt2,     0.,  0.;
             0.,     0.,  0.,     0., -sqrt2,  0.;
             0.,     0.,  0.,     0.,     0., -1.;
             1.,     4.,  3.,     8.,    10.,  6.]);

b = [0., 0., 0., 0., 0., 0., 1. ]';


% generate settings
settings = DefaultSettings();
settings.max_iter = 100;
settings.verbose = 1;
settings.direct_solve_method =  'FAER';
% settings.chordal_decomposition_compact = 'NONE';

cones = [PSDTriangleConeT(n); zeroConeT(1)];

try 
solution = clarabel_mex(P,q,A,b,cones,settings);
catch
end

