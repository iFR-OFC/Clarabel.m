clc
clear


% define cost
P = sparse([6 ,0;0,4]);
q = [-1,-4];

% define constraints
sqrt2 = sqrt(2);
A = sparse([1., -2.
            1.,  0.
            0.,  1.
            1.,  0.
            0., -1.
            ]);

b = [ 0., 1., 1., 1., 1. ]';


cones = [ zeroConeT(1);
          NonnegativeConeT(4);];

% generate settings
settings = DefaultSettings();
settings.max_iter = 200;
settings.verbose =1;
solution = clarabel_mex(P,q,A,b,cones,settings)
