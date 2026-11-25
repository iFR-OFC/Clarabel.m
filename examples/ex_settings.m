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

cones = [PSDTriangleConeT(n); zeroConeT(1)];

% generate settings

fprintf('We make use of the default settings in the C/C++ interface (no settings) \n')
solution_set1 = clarabel_mex(P,q,A,b,cones)

fprintf('We make use of the default settings in the C/C++ interface (empty array) \n')
solution_set2 = clarabel_mex(P,q,A,b,cones, [])


fprintf('We make use of the default settings in the C/C++ interface (empty struct)\n')
settings = struct();
solution_set2 = clarabel_mex(P,q,A,b,cones, settings)



fprintf('We override the complete default settings in the C/C++ interface\n')
settings = DefaultSettings(); % here just a mirror
solution_set3 = clarabel_mex(P,q,A,b,cones, settings)

fprintf('We override only selected settings in the C/C++ interface\n')
settings.direct_solve_method =  'FAER';
settings.max_iter = 10;
settings.verbose = 1;
solution = clarabel_mex(P,q,A,b,cones,settings)




