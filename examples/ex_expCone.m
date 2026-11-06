clc
clear


% define cost
P = sparse(zeros(6,6));
q = [0., 0., -1., 0., 0., -1.];

% define constraints
A = sparse([ -1.,  0.,  0.,  0.,  0.,  0.;
         0., -1.,  0.,  0.,  0.,  0.;
         0.,  0., -1.,  0.,  0.,  0.;
         0.,  0.,  0., -1.,  0.,  0.;
         0.,  0.,  0.,  0., -1.,  0.;
         0.,  0.,  0.,  0.,  0., -1.;
         1.,  2.,  0.,  3.,  0.,  0.;
         0.,  0.,  0.,  0.,  1.,  0.]);

b = [ 0., 0., 0., 0., 0., 0., 3., 1.]';

cones = [
         PowerConeT(0.6);
         PowerConeT(0.1);
         zeroConeT(1);
         zeroConeT(1);
         ];

% generate settings
settings = DefaultSettings();


sol = clarabel_mex(P,q,A,b,cones,settings)
