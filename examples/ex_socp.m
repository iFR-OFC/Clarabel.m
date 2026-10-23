clc
clear


% define cost
P = sparse([0 ,0;0,2]);
q = [0,0];

% define constraints
sqrt2 = sqrt(2);
A = sparse([ 0.,  0.;
            -2.,  0.;
            0., -1.]);

b = [ 1., -2., -2. ]';


% generate settings
settings = DefaultSettings();
settings.max_iter = 200;
settings.verbose =1
cones = SecondOrderConeT(3);

solution = clarabel_mex(P,q,A,b,cones,settings)
