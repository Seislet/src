/* Data structures and common functions used in acoustic/visco-acoustic FWI */
/*
 Copyright (C) 2016 The University of Texas at Austin
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <rsf.h>
#ifdef _OPENMP
#include <omp.h>
#endif
/*^*/

typedef struct sf_mpipar{
	int cpuid;
	int numprocs;
} sf_mpi;
/*^*/

typedef struct sf_source{
	float fhi;
	float flo;
	int rectx;
	int rectz;
} *sf_sou;
/*^*/

typedef struct sf_acquisition{
	// model dimension
	int nx;
	int nz;
	float dx;
	float dz;
	float x0;
	float z0;
	// wavelet dimension
	int nt;
	float dt;
	float t0;
	// absorbing boundary condition
	int nb;
	float coef;
	float *bc;
	// padding
	int padnx;
	int padnz;
	float padx0;
	float padz0;
	// acquisition type
	int acqui_type;
	// shot
	int ns;
	float ds;
	float s0;
	int sz;
	int ds_v;
	int s0_v;
	// receiver
	int nr;
	float dr;
	float r0;
	int rz;
	int dr_v;
	int *r0_v;
	int *r02;
	int *nr2;
	// reference frequency
	float f0;
	// wavefield storing interval
	int interval;
} *sf_acqui;
/*^*/

typedef struct sf_1darray{
	float *vv;
	float *tau;
	float *taus;
	float *ww;
} *sf_vec;
/*^*/

typedef struct sf_fwipar{
	bool onlygrad;
	int grad_type;
	// gradient precondition flag
	int prec;
	int misfit_type;
	int match;
	bool match_opt;
	// iteration number of obtaining matching filter
	int fniter;
	// smoothing parameters for regularization
	int frectx;
	int frectz;
	// number of time shifts 
	int nw;
	// interval of time shifts
	int dw;
	// data residual smoothing parameters
	int drectx;
	int drectz;
	// data residual weighting
	float wt1;
	float wt2;
	float woff1;
	float woff2;
	float gain;
	// water layer depth
	int waterz;
	// gradient smoothing parameters
	int rectx;
	int rectz;
} *sf_fwi;
/*^*/

typedef struct sf_optimization {
	int niter;
	float conv_error;
	int opt_type;
	int npair;
	int nls;
	int igrad;
	int ipair;
	int ils;
	float c1;
	float c2;
	float factor;
	int repeat;
	float alpha;
	float f0;
	float fk;
	float gk_norm;
	float **sk, **yk;
	// bound constraints
	float v1;
	float v2;
	float tau1;
	float tau2;
	int tangent;
	int sigma1;
	int sigma2;
	float *err;
} *sf_optim;
/*^*/

typedef void (*sf_gradient)(float*,float*,float*);
/*^*/

const float c0=-205./72, c1=8./5, c2=-1./5, c3=8./315, c4=-1./560;

void preparation(sf_file Fv, sf_file Fq, sf_file Ftau, sf_file Fw, sf_acqui acpar, sf_sou soupar, sf_vec array)
/*< read data, initialize variables and prepare acquisition geometry >*/
{
	int i, nb, nzx;
	float sx, xend, rbegin, rend, tmp;
	float *qq;

	int nplo=3, nphi=3, nt;
	float eps=0.0001;
	sf_butter blo=NULL, bhi=NULL;

	/* absorbing boundary coefficients */
	nb=acpar->nb;
	acpar->bc=sf_floatalloc(nb);
	for(i=0; i<nb; i++){
		tmp=acpar->coef*(nb-i);
		acpar->bc[i]=expf(-tmp*tmp);
	}

	/* padding variables */
	acpar->padnx=acpar->nx+2*nb;
	acpar->padnz=acpar->nz+2*nb;
	acpar->padx0=acpar->x0-nb*acpar->dx;
	acpar->padz0=acpar->z0-nb*acpar->dz;

	/* acquisition parameters */
	acpar->ds_v=acpar->ds/acpar->dx+0.5;
	acpar->s0_v=(acpar->s0-acpar->x0)/acpar->dx+0.5+nb;
	acpar->sz += nb;

	acpar->dr_v=acpar->dr/acpar->dx+0.5;
	acpar->r0_v=sf_intalloc(acpar->ns);
	acpar->r02=sf_intalloc(acpar->ns);
	acpar->nr2=sf_intalloc(acpar->ns);
	acpar->rz += nb;
	xend=acpar->x0+(acpar->nx-1)*acpar->dx;
	if(acpar->acqui_type==1){
		for(i=0; i<acpar->ns; i++){
			acpar->r0_v[i]=acpar->r0/acpar->dx+0.5+nb;
			acpar->r02[i]=0;
			acpar->nr2[i]=acpar->nr;
		}
	}else{
		for(i=0; i<acpar->ns; i++){
			sx=acpar->s0+acpar->ds*i;
			rbegin=(sx+acpar->r0 <acpar->x0)? acpar->x0 : sx+acpar->r0;
			rend=sx+acpar->r0 +(acpar->nr-1)*acpar->dr;
			rend=(rend < xend)? rend : xend;
			acpar->r0_v[i]=(rbegin-acpar->x0)/acpar->dx+0.5+nb;
			acpar->r02[i]=(rbegin-sx-acpar->r0)/acpar->dx+0.5;
			acpar->nr2[i]=(rend-rbegin)/acpar->dr+1.5;
		}
	}

	/* read model parameters */
	nzx=acpar->nz*acpar->nx;
	nt=acpar->nt;
	array->vv=sf_floatalloc(nzx);
	qq=sf_floatalloc(nzx);
	array->tau=sf_floatalloc(nzx);
	array->taus=sf_floatalloc(nzx);
	array->ww=sf_floatalloc(nt);

	sf_floatread(array->vv, nzx, Fv);
	sf_floatread(qq, nzx, Fq);
	sf_floatread(array->tau, nzx, Ftau);
	sf_floatread(array->ww, nt, Fw);

	/* calculate taus */
	for(i=0; i<nzx; i++){
		array->taus[i]=(sqrtf(qq[i]*qq[i]+1)-1.)/(2.*SF_PI*acpar->f0*qq[i]);
	}

	/* bandpass the wavelet */
	soupar->flo *= acpar->dt;
	soupar->fhi *= acpar->dt;
	if(soupar->flo > eps) blo=sf_butter_init(false, soupar->flo, nplo);
	if(soupar->fhi < 0.5-eps) bhi=sf_butter_init(true, soupar->fhi, nphi);

	if(NULL != blo){
		sf_butter_apply(blo, nt, array->ww);
		sf_reverse(nt, array->ww);
		sf_butter_apply(blo, nt, array->ww);
		sf_reverse(nt, array->ww);
		sf_butter_close(blo);
	}
	if(NULL != bhi){
		sf_butter_apply(bhi, nt, array->ww);
		sf_reverse(nt, array->ww);
		sf_butter_apply(bhi, nt, array->ww);
		sf_reverse(nt, array->ww);
		sf_butter_close(bhi);
	}
	
	free(qq);
}

void pad2d(float *vec, float **array, int nz, int nx, int nb)
/*< convert a vector to an array >*/
{
	int ix, iz;
	
	for(ix=0; ix<nx; ix++){
		for(iz=0; iz<nz; iz++){
			array[ix+nb][iz+nb]=vec[ix*nz+iz];
		}
	}

    for (ix=nb; ix<nx+nb; ix++){
		for (iz=0; iz<nb; iz++){
			array[ix][iz]=array[ix][nb];
			array[ix][iz+nz+nb]=array[ix][nz+nb-1];
		}
	}

	for (ix=0; ix<nb; ix++){
		for (iz=0; iz<nz+2*nb; iz++){
			array[ix][iz]=array[nb][iz];
			array[ix+nx+nb][iz]=array[nx+nb-1][iz];
		}
	}
}

void source_map(int sx, int sz, int rectx, int rectz, int padnx, int padnz, int padnzx, float *rr)
/*< generate source map >*/
{
	int i, j, i0;
	int n[2], s[2], rect[2];
	bool diff[2], box[2];
	sf_triangle tr;

	n[0]=padnz; n[1]=padnx;
	s[0]=1; s[1]=padnz;
	rect[0]=rectz; rect[1]=rectx;
	diff[0]=false; diff[1]=false;
	box[0]=false; box[1]=false;

	for (i=0; i<padnzx; i++)
		rr[i]=0.;
	j=sx*padnz+sz;
	rr[j]=1.;

	for (i=0; i<2; i++){
		if(rect[i] <=1) continue;
		tr=sf_triangle_init(rect[i], n[i], box[i]);
		for(j=0; j<padnzx/n[i]; j++){
			i0=sf_first_index(i,j,2,n,s);
			sf_smooth2(tr,i0,s[i],diff[i],rr);
		}
		sf_triangle_close(tr);
	}
}

void laplace(float **p1, float **term, int padnx, int padnz, float dx2, float dz2)
/*< laplace operator >*/
{
	int ix, iz;

#ifdef _OPENMP
#pragma omp parallel for \
	private(ix,iz) \
	shared(padnx,padnz,p1,term)
#endif

	for (ix=4; ix<padnx-4; ix++){
		for (iz=4; iz<padnz-4; iz++){
			term[ix][iz] = 
				(c0*p1[ix][iz]
				+c1*(p1[ix+1][iz]+p1[ix-1][iz])
				+c2*(p1[ix+2][iz]+p1[ix-2][iz])
				+c3*(p1[ix+3][iz]+p1[ix-3][iz])
				+c4*(p1[ix+4][iz]+p1[ix-4][iz]))/dx2 
				+(c0*p1[ix][iz]
				+c1*(p1[ix][iz+1]+p1[ix][iz-1])
				+c2*(p1[ix][iz+2]+p1[ix][iz-2])
				+c3*(p1[ix][iz+3]+p1[ix][iz-3])
				+c4*(p1[ix][iz+4]+p1[ix][iz-4]))/dz2;
		}
	}
}

void apply_sponge(float **p, float *bc, int padnx, int padnz, int nb)
/*< apply absorbing boundary condition >*/
{
	int ix, iz;

	for (ix=0; ix<padnx; ix++){
		for(iz=0; iz<nb; iz++){	// top ABC
			p[ix][iz]=bc[iz]*p[ix][iz];
		}
		for(iz=padnz-nb; iz<padnz; iz++){ // bottom ABC			
			p[ix][iz]=bc[padnz-iz-1]*p[ix][iz];
		} 
	}

	for (iz=0; iz<padnz; iz++){
		for(ix=0; ix<nb; ix++){ // left ABC			
			p[ix][iz]=bc[ix]*p[ix][iz];
		}
		for(ix=padnx-nb; ix<padnx; ix++){ // right ABC			
			p[ix][iz]=bc[padnx-ix-1]*p[ix][iz];
		}
	}
}

void residual_weighting(float **ww, int nt, int nr, int wtn1, int wtn2, int woffn1, int woffn2, float gain)
/*< data residual weighting >*/
{
	int it, ir;
	float w[10];

	for(it=0; it<10; it++){
		w[it]=sinf(0.5*SF_PI*(it+1)/11.);
	}

	for(ir=0; ir<nr; ir++){
		for(it=0; it<nt; it++){
			ww[ir][it]=0.;
		}
	}

	for(ir=woffn1; ir<=woffn2; ir++){
		for(it=wtn1; it<=wtn2; it++){
			ww[ir][it]=1.+(gain-1.)*(it-wtn1)/(wtn2-wtn1);
		}
	}

	for(ir=0; ir<10; ir++){
		for(it=wtn1; it<=wtn2; it++){
			ww[woffn1+ir][it] *= w[ir];
			ww[woffn2-ir][it] *= w[ir];
		}
	}

	for(it=0; it<10; it++){
		for(ir=woffn1; ir<=woffn2; ir++){
			ww[ir][wtn1+it] *= w[it];
			ww[ir][wtn2-it] *= w[it];
		}
	}

}

void gradient_smooth2(int rectx, int rectz, int nx, int nz, int waterz, float scaling, float *grad)
/*< smooth gradient, zero bathymetry layer and normalization >*/
{
	int i, j, i0, nzx;
	int n[2], s[2], rect[2];
	bool diff[2], box[2];
	sf_triangle tr;

	nzx=nz*nx;
	n[0]=nz; n[1]=nx;
	s[0]=1; s[1]=nz;
	rect[0]=rectz; rect[1]=rectx;
	diff[0]=false; diff[1]=false;
	box[0]=false; box[1]=false;

	for (i=0; i<2; i++){
		if(rect[i] <=1) continue;
		tr=sf_triangle_init(rect[i], n[i], box[i]);
		for(j=0; j<nzx/n[i]; j++){
			i0=sf_first_index(i,j,2,n,s);
			sf_smooth2(tr,i0,s[i],diff[i],grad);
		}
		sf_triangle_close(tr);
	}

	for(i=0; i<waterz; i++)
		for(j=0; j<nx; j++)
			grad[i+j*nz]=0.;

	for(i=0; i<nzx; i++)
		grad[i] *= scaling;
}

/* ------------------------------------------------------------------------------- */

int irr;
float **aa;

void wiener_lop(bool adj, bool add, int nx, int ny, float *xx, float *yy)
/*< linear operator >*/
{
	int i, j;

	sf_adjnull(adj, add, nx, ny, xx, yy);

	if(adj){
#ifdef _OPENMP
#pragma omp parallel for \
	private(i,j) \
	shared(nx,aa,yy,xx)
#endif
		for(i=0; i<nx; i++){
			for(j=0; j<nx-i; j++){
				xx[i] += aa[irr][j]*yy[j+i];
			}
		}
	}else{
#ifdef _OPENMP
#pragma omp parallel for \
	private(i,j) \
	shared(nx,aa,yy,xx)
#endif
		for(i=0; i<nx; i++){
			for(j=0; j<=i; j++){
				yy[i] += aa[irr][j]*xx[i-j];
			}
		}
	}
}

void wiener_lop2(bool adj, bool add, int nx, int ny, float *xx, float *yy)
/*< linear operator >*/
{
	int i, j;

	sf_adjnull(adj, add, nx, ny, xx, yy);
	
	if(adj){
#ifdef _OPENMP
#pragma omp parallel for \
	private(i,j) \
	shared(nx,aa,yy,xx)
#endif
		for(i=0; i<nx; i++){
			for(j=0; j<=i; j++){
				xx[i] += aa[irr][j]*yy[i-j];
			}
		}
	}else{
#ifdef _OPENMP
#pragma omp parallel for \
	private(i,j) \
	shared(nx,aa,yy,xx)
#endif
		for(i=0; i<nx; i++){
			for(j=0; j<nx-i; j++){
				yy[i] += aa[irr][j]*xx[j+i];
			}
		}
	}
}

void adjsou_wiener(float **dd, float **pp, float *fcost, int nr, int r0, int nt, float dt, int fniter)
/*< calculate adjoint source of Wiener filter misfit >*/
{
	int i1, i2;
	float nw, ntw, misfit, *filter;

	aa=dd;
	filter=sf_floatalloc(nt);

	for(i2=r0; i2<r0+nr; i2++){
		
		irr=i2;

		/* Wiener filter */
		sf_solver(wiener_lop, sf_cgstep, nt, nt, filter, pp[i2], fniter, "end");
		
		/* Weighting */
		nw=0.;
		ntw=0.;
		for(i1=0; i1<nt; i1++){
			nw += filter[i1]*filter[i1];
			ntw += (i1*dt)*(i1*dt)*filter[i1]*filter[i1];
		}
		misfit=ntw/nw/2.0;
		*fcost += misfit;
		for(i1=0; i1<nt; i1++){
			filter[i1]=filter[i1]*((i1*dt)*(i1*dt)-2.*misfit)/nw;
		}

		/* Optimization */
		sf_solver(wiener_lop2, sf_cgstep, nt, nt, pp[i2], filter, fniter, "end");

		/* reverse the sign */
		for(i1=0; i1<nt; i1++){
			pp[i2][i1]=-pp[i2][i1];
		}
	}

	free(filter);
}

/* ------------------------------------------------------------------------------- */

void smooth_misfit(float **pp, float *fcost, int nr, int nt, int drectx, int drectz, int ider)
/*< adjoint source of new smoothing-weighted misfit >*/
{
	int it, ir, nd;
	int m[2], rect[2];

	nd=nt*nr;
	m[0]=nt; m[1]=nr;
	rect[0]=drectz; rect[1]=drectx;

	sf_dtrianglen_init(2, rect, m);
	
	sf_dtrianglen(0, 1, 6, pp[0]);
	
	for (ir=0; ir<nr; ir++)
		for (it=0; it<nt; it++)
			*fcost += 0.5*pp[ir][it]*pp[ir][it];

	sf_dtrianglen(ider, 1, 6, pp[0]);
	
	if(ider!=0){
		for (ir=0; ir<nr; ir++)
			for (it=0; it<nt; it++)
				pp[ir][it] *=2.;
	}

	sf_dtrianglen_close();
}

///* ------------------------------------------------------------------------------- */
//
//void smooth_misfit(float **pp, float *fcost, int nr, int nt, int drectx, int drectz)
///*< adjoint source of new smoothing-weighted misfit >*/
//{
//	int it, ir, nd;
//	int m[2], rect[2];
//	float *temp;
//
//	nd=nt*nr;
//	m[0]=nt; m[1]=nr;
//	rect[0]=drectz; rect[1]=drectx;
//	temp=sf_floatalloc(nd);
//
//	sf_trianglen_init(2, rect, m);
//	
//	sf_trianglen_lop(false,false, nd, nd, pp[0], temp);
//	
//	for (ir=0; ir<nd; ir++)
//		*fcost += 0.5*temp[ir]*temp[ir];
//
//	sf_trianglen_lop(true,false, nd, nd, pp[0], temp);
//
//	sf_trianglen_close();
//	free(temp);
//}

/* ------------------------------------------------------------------------------- */
void adjsou_match(float **dd, float **pp, float *fcost, int nw, int dw, float w0, int nr, int nt, float dt, int fniter, int frectx, int frectz, int match, bool match_opt)
/*< calculate adjoint source of nonstationary matching filtering misfit >*/
{
	sf_map4 mo;
	int it, ir, iw, n12, nd, shift;
	int m[2], rect[2];
	float tmp, tshift, fn, fwn, misfit, mean;
	float *str, ***ref, ***fil;

	m[0]=nt; m[1]=nr;
	rect[0]=frectz; rect[1]=frectx;
	n12=nt*nr;
	nd=n12*nw;

	ref=sf_floatalloc3(nt, nr, nw);
	fil=sf_floatalloc3(nt, nr, nw);

	/* construct references */
	memset(ref[0][0], 0., nd*sizeof(float));
	if(match==0){
		for(iw=0; iw<=nw/2; iw++){
			shift=(nw/2-iw)*dw;
			for(ir=0; ir<nr; ir++){
				for(it=shift; it<nt; it++){
					ref[iw][ir][it-shift]=dd[ir][it];
				}
			}
		}
		for(iw=1; iw<=nw/2; iw++){
			shift=iw*dw;
			for(ir=0; ir<nr; ir++){
				for(it=0; it<nt-shift; it++){
					ref[iw+nw/2][ir][it+shift]=dd[ir][it];
				}
			}
		}
	}else{
		str=sf_floatalloc(nt);
		mo=sf_stretch4_init(nt, 0., dt, nt, 0.01);
		for(iw=0; iw<nw; iw++){
			for(it=0; it<nt; it++){
				str[it]=it*dt*(iw*dw*dt+w0);
			}
			
			for(ir=0; ir<nr; ir++){
				sf_stretch4_define(mo,str);
				sf_stretch4_apply(false,mo,dd[ir],ref[iw][ir]);
			}
		}
		sf_stretch4_close(mo);
	}

	/* scaling */
	mean=0.;
	for(iw=0; iw<nw; iw++){
		for(ir=0; ir<nr; ir++){
			for(it=0; it<nt; it++){
				mean += ref[iw][ir][it]*ref[iw][ir][it];
			}
		}
	}
	mean=sqrtf(mean/nd);
	for(iw=0; iw<nw; iw++){
		for(ir=0; ir<nr; ir++){
			for(it=0; it<nt; it++){
				ref[iw][ir][it] /= mean;
			}
		}
	}
	for(ir=0; ir<nr; ir++){
		for(it=0; it<nt; it++){
			pp[ir][it] /= mean;
		}
	}

	/* invert for matching filter */
	sf_multidivn_init(nw, 2, n12, m, rect, ref[0][0], NULL, false);
	sf_multidivn(pp[0], fil[0][0], fniter);

	/* weighting */
	for(ir=0; ir<nr; ir++){
		fn=0.;
		fwn=0.;
		for(iw=0; iw<nw; iw++){
			tshift=w0+iw*dw*dt-match;
			for(it=0; it<nt; it++){
				tmp = fil[iw][ir][it]*fil[iw][ir][it];
				fn += tmp;
				fwn += tmp*tshift*tshift;
			}
		}
		
		misfit=fwn/fn/2.0;
		*fcost += misfit;
		
		for(iw=0; iw<nw; iw++){
			tshift=w0+iw*dw*dt-match;
			for(it=0; it<nt; it++){
				fil[iw][ir][it] = fil[iw][ir][it]*(tshift*tshift-2.*misfit)/fn;
			}
		}
	}

	/* apply operator D */
	for(iw=0; iw<nw; iw++){
		for(ir=0; ir<nr; ir++){
			for(it=0; it<nt; it++){
				ref[iw][ir][it] *= (-1.*mean);
			}
		}
	}

	if(match_opt){
		sf_multidivn_adj(false, pp[0], fil[0][0], fniter);
	}else{
		sf_weight2_lop(false, false, nd, n12, fil[0][0], pp[0]);
	}

	/* free allocated storage */
	sf_multidivn_close();

	free(**ref); free(*ref); free(ref);
	free(**fil); free(*fil); free(fil);
	if(match==1) free(str);
}

/* ------------------------------------------------------------------------------- */

void l2norm(int n, float *a, float *norm)
/*< L2 norm of a vector >*/
{
	int i;
	*norm=0.;
	for(i=0; i<n; i++){
		*norm += a[i]*a[i];
	}
	*norm=sqrtf(*norm);
}

void reverse(int n, float *a, float *b)
/*< reverse the sign of the vector >*/
{
	int i;
	for (i=0; i<n; i++)
		b[i]=-a[i];
}

void copy(int n, float *a, float *b)
/*< copy vector >*/
{
	int i;
	for(i=0; i<n; i++)
		b[i]=a[i];
}

void dot_product(int n, float *a, float *b, float *product)
/*< dot product of two vectors >*/
{
	int i;
	*product=0.;
	for (i=0; i<n; i++)
		*product += a[i]*b[i];
}

void print_iteration(FILE *fp, int iter, sf_optim opt)
/*< print out iteration information >*/
{
	if(iter%10==0){
		fprintf(fp,"*********************************************\n");
		fprintf(fp,"Maximum Iteration: %d\n", opt->niter);
		fprintf(fp,"Convergence Error: %3.2e\n", opt->conv_error);
		fprintf(fp,"*********************************************\n");
		fprintf(fp,"Niter  Misfit  Rel_Misfit  Grad_Norm  Alpha   Num_Pair  Num_LS  Total_Grad\n");
	}
	fprintf(fp,"%3d   %3.2e  %3.2e   %3.2e  %3.2e  %3d       %3d      %4d\n", iter, opt->fk, opt->fk/opt->f0, opt->gk_norm, opt->alpha, opt->ipair, opt->ils, opt->igrad);
	/* get written to disk right away */
	fclose(fp);
	fp=fopen("iterate.txt","a");
}
