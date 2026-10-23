clc
clear


% define cost
P = sparse([0 ,0;0,0]);
q = [1,-1];

% define constraints
sqrt2 = sqrt(2);
A = sparse([ 1.,  0.;
             0.,  1.;
            -1.,  0.;
             0., -1]);

b = [ 1.0, 1.0, 1.0, 1.0 ]';

cones = NonnegativeConeT(4);

% generate settings
settings = DefaultSettings();
settings.equilibrate_enable = true;
settings.max_iter = 50;
settings.verbose = 1;

solution = clarabel_mex(P, q, A, b, cones, settings)
