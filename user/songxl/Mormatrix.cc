// Lowrank decomposition for 3-D orthorhombic wave propagation. 
//   Copyright (C) 2010 University of Texas at Austin
//  
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation; either version 2 of the License, or
//   (at your option) any later version.
//  
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//  
//   You should have received a copy of the GNU General Public License
//   along with this program; if not, write to the Free Software
//   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#include <time.h>

#include <rsf.hh>

#include "vecmatop.hh"
#include "serialize.hh"

using namespace std;

static std::valarray<float>  vx, vy, vz, yi1, yi2, mu;
static std::valarray<float> kx, ky, kz;
static float dt;

int sample(vector<int>& rs, vector<int>& cs, FltNumMat& res)
{
    int nr = rs.size();
    int nc = cs.size();
    res.resize(nr,nc);  
    setvalue(res,0.0f);
    float con2 = pow(2.0,1/3.0);
    for(int a=0; a<nr; a++) {
        int i=rs[a];
        float wx = vx[i]*vx[i];
        float wy = vy[i]*vy[i];
        float wz = vz[i]*vz[i];
        float e1 = yi1[i];
        float e2 = yi2[i];
        float e3 = mu[i];

	for(int b=0; b<nc; b++) {
           int j=cs[b];
           float x=kx[j]*kx[j];
           float y=ky[j]*ky[j];
           float z=kz[j]*kz[j];
           
           float aa=(2*e1+1)*wx*x+(2*e2+1)*wy*y+wz*z;
           float bb=wx*wx*x*y*(2*e1*e3+e3)*(2*e1*e3+e3)-wx*wy*(2*e1+1)*(2*e2+1)*x*y-2*wx*wz*e1*x*z-2*wy*wz*e2*y*z;
           //float cc=-(wz*z)*(wx*x)*(wx*y)*(2*e1*e3+e3)*(2*e1*e3+e3)+2*(wz*z)*(wx*x)*(vx[i]*y*vy[i])*e3*(2*e1+1)-(wx*x)*(wy*y)*(wz*z)*(1-4*e1*e2);
           float cc=(wz*z)*(wx*x)*y*(-(wx)*(2*e1*e3+e3)*(2*e1*e3+e3)+2*(vx[i]*vy[i])*e3*(2*e1+1)-(wy)*(1-4*e1*e2));
           //float dd=(-(wx)*(2*e1*e3+e3)*(2*e1*e3+e3)+2*(vx[i]*vy[i])*e3*(2*e1+1)-(wy)*(1-4*e1*e2));
           // cerr<<"aa="<<aa<<" ";    cerr<<endl;
           // cerr<<"bb="<<bb<<" ";    cerr<<endl;
          // if(cc) {  cerr<<"cc="<<cc<<" ";    cerr<<endl; }
          //  if (dd) {cerr<<"dd="<<dd<<" ";    cerr<<endl; }
           float r = (81*cc+6*aa*(2*aa*aa+9*bb))*cc-3*bb*bb*(aa*aa+4*bb);
         //   if (r>0) {cerr<<"r="<<r<<" ";    cerr<<endl;}
            //cerr<<"r="<<r<<" ";    cerr<<endl;
            r = sqrt(abs(r))-9*cc;
            float mm = -2*aa*aa*aa+3*r-9*aa*bb;
            if (mm<0) r = -pow(-mm,float(1.0/3.0));
            else r = pow(mm,float(1.0/3.0));
      //      cerr<<r<<" ";    cerr<<endl;
            if (abs(r) < 0.000001) {r = 0.0;}
            else { r = 1/6.0*(-con2*con2*r-2*con2*(aa*aa+3*bb)/r+2*aa);} 
       //     if (r < 0) cerr<<r<<endl;
            r = sqrt(abs(r));
      //      r = sqrt(wz*(x+y+z));
      //      cerr<<r<<" ";    cerr<<endl;
	   res(a,b) = cos(r*dt); 
	 // res(a,b) = 2*(cos(r*dt)-1); 
	}
    }
    return 0;
}

int main(int argc, char** argv)
{   
    sf_init(argc,argv); // Initialize RSF

    iRSF par(0);
    int seed;

    par.get("seed",seed,time(NULL)); // seed for random number generator
    srand48(seed);

    float eps;
    par.get("eps",eps,1.e-4); // tolerance

    int npk;
    par.get("npk",npk,20); // maximum rank

    int xx1, xx2, xx3;
    par.get("xx1",xx1); // x location
    par.get("xx2",xx2); // x location
    par.get("xx3",xx3); // x location

    par.get("dt",dt); // time step

    iRSF velz, velx("velx"), vely("vely"), etax("etax"), etay("etay"), muz("mu");

    int nz,nx,ny;
    velz.get("n1",nz);
    velz.get("n2",nx);
    velz.get("n3",ny);
    int m = nx*ny*nz;

    vx.resize(m);
    vy.resize(m);
    vz.resize(m);
    yi1.resize(m);
    yi2.resize(m);
    mu.resize(m);
    velx >> vx;
    vely >> vy;
    velz >> vz;
    etax >> yi1;
    etay >> yi2;
    muz  >> mu;
    
    iRSF fft("fft");

    int nkz,nkx,nky;
    fft.get("n1",nkz);
    fft.get("n2",nkx);
    fft.get("n3",nky);

    float dkz,dkx,dky;
    fft.get("d1",dkz);
    fft.get("d2",dkx);
    fft.get("d3",dky);
    
    float kz0,kx0,ky0;
    fft.get("o1",kz0);
    fft.get("o2",kx0);
    fft.get("o3",ky0);


    int n = nkx*nky*nkz;
    kx.resize(n);
    ky.resize(n);
    kz.resize(n);
    int i = 0;
    for (int iy=0; iy < nky; iy++) {
        for (int ix=0; ix < nkx; ix++) {
            for (int iz=0; iz < nkz; iz++) {
                kx[i] = 2*SF_PI*(kx0+ix*dkx);
                ky[i] = 2*SF_PI*(ky0+iy*dky);
                kz[i] = 2*SF_PI*(kz0+iz*dkz);
                i++;
            }
        }
    }


    vector<int> lidx, ridx;
    FltNumMat mid;

    iC( lowrank(m,n,sample,(float) eps,npk,lidx,ridx,mid) );

    int m2=mid.m();
    int n2=mid.n();


    vector<int> midx(m), nidx(n), xidx(3);
    for (int k=0; k < m; k++) 
	midx[k] = k;
    for (int k=0; k < n; k++) 
	nidx[k] = k;    
    xidx[0] = xx1;
    xidx[1] = xx2;
    xidx[3] = xx3;

    float *dmid = mid.data();
    std::valarray<float> fmid(m2*n2);
    for (int k=0; k < m2*n2; k++) {
        fmid[k] = dmid[k];
    }

    oRSF middle;
    middle.put("n1",m2);
    middle.put("n2",n2);
    middle.put("n3",1);
    middle << fmid;

    //FltNumMat lmat(m,m2);
    FltNumMat lmat(3,m2);
    iC ( sample(xidx,lidx,lmat) );

    FltNumMat rmat(n2,n);
    iC ( sample(ridx,nidx,rmat) );

    FltNumMat tmpmat(m2,n), appmat(3,n);
    iC(dgemm(1.0,mid,rmat,0.0,tmpmat));
    iC(dgemm(1.0,lmat,tmpmat,0.0,appmat));
    
    float *appdat = appmat.data();

    std::valarray<float> appdata(3*n);    
    for (int k=0; k < 3*n; k++) 
	appdata[k] = appdat[k];
    oRSF app("app");
    app.put("n1",3);
    app.put("n2",nkz);
    app.put("n3",nkx);
    app.put("n4",nky);
    app.put("d1",1);
    app.put("d2",dkz);
    app.put("d3",dkx);
    app.put("d4",dky);
    app.put("o1",1);
    app.put("o2",kz0);
    app.put("o3",kx0);
    app.put("o4",ky0);
    app << appdata;

    FltNumMat realmat(3,n);
    iC ( sample(xidx,nidx,realmat) );
    float *realdat = realmat.data();

    std::valarray<float> realdata(3*n);    
    for (int k=0; k < 3*n; k++) 
	realdata[k] = realdat[k];
    oRSF real("real");
    real.put("n1",3);
    real.put("n2",nkz);
    real.put("n3",nkx);
    real.put("n4",nky);
    app.put("d1",1);
    app.put("d2",dkz);
    app.put("d3",dkx);
    app.put("d4",dky);
    app.put("o1",1);
    app.put("o2",kz0);
    app.put("o3",kx0);
    app.put("o4",ky0);
    real << realdata;



    return 0;
}
