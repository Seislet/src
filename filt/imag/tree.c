#include <math.h>

#include <rsf.h>

#include "tree.h"
#include "node.h"
#include "eno2.h"
#include "cell.h"

#ifndef MIN
#define MIN(a,b) ((a)<(b))?(a):(b)
#endif

static Node Tree;
static NodeList Orphans;

static eno2 cvel;

static int nz, nx, na, nt, nax, naxz, order;
static float dz, dx, da, z0, x0, a0, **val;
static const float eps = 1.e-5;
static bool *accepted;

static void psnap (float* p, float* q, int* iq);

void tree_init (int order1,
		int nz1, int nx1, int na1, int nt1, 
		float dz1, float dx1, float da1, 
		float z01, float x01, float a01, 
		float** vel, float** value) {
    nx = nx1; nz = nz1; na = na1; nt = nt1;
    dx = dx1; dz = dz1; da = da1;
    x0 = x01; z0 = z01; a0 = a01;

    nax = na*nx; naxz = nax*nz;
    order = order1;
    
    cvel = eno2_init (order, nz, nx);
    eno2_set (cvel, vel); /* vel is slowness */

    val = value;
    accepted = sf_boolalloc(naxz);

    Orphans = CreateNodeList(nax);
    Tree = CreateNodes(naxz,order);
}

void tree_build(void)
{
    int i, k, iz, ix, ia, kx, kz, ka, jx, jz;
    float x, z, p[2], a, v, v0, g0[2], g[2], s, sx, sz, t, *vk;
    bool onx, onz;
    Node node;

    sf_warning("Method=%d",order);
    for (kz=0; kz < nz; kz++) {
	sf_warning("Building %d of %d",kz+1,nz);
	for (kx=0; kx < nx; kx++) {
	
	    eno2_apply(cvel,kz,kx,0.,0.,&v0,g0,BOTH);
	    g0[1] /= dx;
	    g0[0] /= dz;

	    for (ka=0; ka < na; ka++) {
		k = ka + kx*na + kz*nax;
		node = Tree+k;

		a = a0 + ka*da;
		p[0] = -cos(a);
		p[1] = sin(a);

		/* get boundary conditions */
		if ((kx==0    && p[1] < 0.) ||
		    (kx==nx-1 && p[1] > 0.) ||
		    (kz==0    && p[0] < 0.) ||
		    (kz==nz-1 && p[0] > 0.)) {
		    AddNode(Orphans,node);
		    vk = val[k];
		    vk[0] = x0 + kx*dx;
		    vk[1] = z0 + kz*dz;
		    vk[2] = 0.;
		    vk[3] = cell_p2a (p);
		    accepted[k] = true;
		    continue;
		} else {
		    accepted[k] = false;
		}

		ia = ka;  
		x = 0.; ix=kx;
		z = 0.; iz=kz;		
		v = v0;
		g[0] = g0[0];
		g[1] = g0[1];

		switch (order) {
		    case 2:
			t = cell1_update2 (2, 0., v, p, g);
			/* p is normal vector now ||p|| = 1 */
	    
			cell1_intersect (g[1],x,dx/v,p[1],&sx,&jx);
			cell1_intersect (g[0],z,dz/v,p[0],&sz,&jz);
	    
			s = MIN(sx,sz);
	    
			t += cell1_update1 (2, s, v, p, g);
			/* p is slowness vector now ||p||=v */
	    
			if (s == sz) {
			    z = 0.; iz += jz;
			    x += p[1]*s/dx;
			} else {
			    x = 0.; ix += jx;
			    z += p[0]*s/dz;
			}
	    
			onz = cell_snap (&z,&iz,eps);
			onx = cell_snap (&x,&ix,eps);
	    
			eno2_apply(cvel,iz,ix,z,x,&v,g,BOTH);
			g[1] /= dx;
			g[0] /= dz;
	    
			t += cell1_update2 (2, s, v, p, g);
			/* p is normal vector now ||p||=1 */
			psnap (p,&a,&ia);
			break;
		    case 3:
			t = cell_update2 (2, 0., v, p, g);
			/* p is normal vector now ||p|| = 1 */
	    
			cell_intersect (g[1],x,dx/v,p[1],&sx,&jx);
			cell_intersect (g[0],z,dz/v,p[0],&sz,&jz);
	    
			s = MIN(sx,sz);
	    
			t += cell_update1 (2, s, v, p, g);
			/* p is slowness vector now ||p||=v */
	    
			if (s == sz) {
			    z = 0.; iz += jz;
			    x += p[1]*s/dx;
			} else {
			    x = 0.; ix += jx;
			    z += p[0]*s/dz;
			}
	    
			onz = cell_snap (&z,&iz,eps);
			onx = cell_snap (&x,&ix,eps);
	    
			eno2_apply(cvel,iz,ix,z,x,&v,g,BOTH);
			g[1] /= dx;
			g[0] /= dz;
	    
			t += cell_update2 (2, s, v, p, g);
			/* p is normal vector now ||p||=1 */
			psnap (p,&a,&ia);
			break;
		    default:
			sf_error("Unknown method");
			break;
		}
  		
		/* pathological exits */
		if (ix < 0 || ix > nx-1 ||
		    iz < 0 || iz > nz-1) {
		    AddNode(Orphans,node);
		    vk = val[k];
		    vk[0] = x0 + kx*dx;
		    vk[1] = z0 + kz*dz;
		    vk[2] = 0.;
		    vk[3] = cell_p2a (p);
		    accepted[k] = true;
		    continue;
		} 

		i = ia + ix*na + iz*nax;

		node->t = t;
		if (onz) { /* hits a z wall */
		    node->w1 = a;
		    node->w2 = x;
		    if (x != 1. && a != 1.) AddChild(Tree,i,0,0,node);
		    if (ia == na-1) {
			node->n1 = 1;
		    } else {
			node->n1 = order;
			if (x != 1. && a != 0.) AddChild(Tree,i+1,0,1,node);
		    }

		    if (ix == nx-1) {
			node->n2 = 1;
		    } else {
			node->n2 = order;
			if (x != 0. && a != 1.) AddChild(Tree,i+na,1,0,node);
		    }

		    if (node->n1 == order && node->n2 == order && 
			x != 0. && a != 0.) 
			AddChild(Tree,i+na+1,1,1,node);

		    if (3==order) {
			if (ia == 0) {
			    if (node->n1 > 1) node->n1 = 2;
			} else if (x != 1. && a != 0. && a != 1.) {
				AddChild(Tree,i-1,0,2,node);
			}

			if (ix == 0) {
			    if (node->n2 > 1) node->n2 = 2;
			} else if (x != 0. && x != 1. && a != 1.) {
			    AddChild(Tree,i-na,2,0,node);
			}

			if (x != 0. && a != 0. && 
			    node->n1 > 1 && node->n2 > 1) {
			    if (node->n1 == 3 && a != 1.) { 
				AddChild(Tree,i+na-1,1,2,node);
				if (node->n2 == 3 && x != 1.)
				    AddChild(Tree,i-na-1,2,2,node);
			    }
			    if (node->n2 == 3 && x != 1.) 
				AddChild(Tree,i-na+1,2,1,node);
			}

		    }
		} else { /* hits an x wall */
		    node->w1 = a;
		    node->w2 = z;
		    if (z != 1. && a != 1.) AddChild(Tree,i,0,0,node);
		    if (ia == na-1) {
			node->n1 = 1;
		    } else {
			node->n1 = order;
			if (z != 1. && a != 0.) AddChild(Tree,i+1,0,1,node);
		    }

		    if (iz == nz-1) {
			node->n2 = 1;
		    } else {
			node->n2 = order;
			if (z != 0. && a != 1.) AddChild(Tree,i+nax,1,0,node);
		    }

		    if (node->n1 == order && node->n2 == order && 
			z != 0. && a != 0.) 
			AddChild(Tree,i+nax+1,1,1,node);

		    if (3==order) {
			if (ia == 0) {
			    if (node->n1 > 1) node->n1 = 2;
			} else if (z != 1. && a != 0. && a != 1.) {
				AddChild(Tree,i-1,0,2,node);
			}

			if (iz == 0) {
			    if (node->n2 > 1) node->n2 = 2;
			} else if (z != 0. && z != 1. && a != 1.) {
			    AddChild(Tree,i-nax,2,0,node);
			}
			
			if (z != 0. && a != 0. && 
			    node->n1 > 1 && node->n2 > 1) {
			    if (node->n1 == 3 && a != 1.) { 
				AddChild(Tree,i+nax-1,1,2,node);
				if (node->n2 == 3 && z != 1.)
				    AddChild(Tree,i-nax-1,2,2,node);
			    }
			    if (node->n2 == 3 && z != 1.) 
				AddChild(Tree,i-nax+1,2,1,node);
			}
		    }
		}
	    }
	}
    }
}

void tree_print (void) {
    int k, ic;
    Node node;

    for (k=0; k < naxz; k++) {
	node = Tree+k;
	fprintf(stderr,"Node %d, nparents=%d, children: ",k,node->nparents);
	for (ic=0; ic < node->children->nitems; ic++) {
	    fprintf(stderr,"%d ",node->children->list[ic]-Tree);
	}
	fprintf(stderr,"\n");
    }
    fprintf(stderr,"Orphans: ");
    for (ic=0; ic < Orphans->nitems; ic++) {
	fprintf(stderr,"%d ",Orphans->list[ic]-Tree);
    }
    fprintf(stderr,"\n");
} 


void tree_traverse (void) {
    Node node, child;
    int k, i, j, k1, k2, n, nc, **parents;
    float x, w1[3], w2[3], *fk;

    for (n=0; n < Orphans->nitems; n++) {
	if (0==n%nax) fprintf(stderr,"Got %d of %d\n",n+1,naxz);

	node = Orphans->list[n];
	k = node - Tree;
	if (!accepted[k]) { /*evaluate node */      
	    fk = val[k];

	    fk[0] = 0.;
	    fk[1] = 0.;
	    fk[2] = node->t;
	    fk[3] = 0.;

	    parents = node->parents;

	    x = node->w1; 
	    switch (node->n1) {
		case 1:
		    w1[0] = 1;
		    break;
		case 2:
		    w1[0] = 1.-x; 
		    w1[1] = x;
		    break;
		case 3:
		    w1[0] = 1.-x*x; 
		    w1[1] = 0.5*x*(x+1.); 
		    w1[2] = 0.5*x*(x-1.);
		    break;
	    }

	    x = node->w2; 
	    switch (node->n1) {
		case 1:
		    w2[0] = 1;
		    break;
		case 2:
		    w2[0] = 1.-x; 
		    w2[1] = x;
		    break;
		case 3:
		    w2[0] = 1.-x*x; 
		    w2[1] = 0.5*x*(x+1.); 
		    w2[2] = 0.5*x*(x-1.);
		    break;
	    }

	    for (k2=0; k2 < node->n2; k2++) {		  
		for (k1=0; k1 < node->n1; k1++) {
		    i = parents[k2][k1];
		    if (i >= 0) {
			x = w2[k2]*w1[k1];
			for (j=0; j < 4; j++) {
			    fk[j] += x*val[i][j];
			}
		    }
		}
	    }
      
	    accepted[k] = true;
	}

	for (nc=0; nc < node->children->nitems; nc++) {
	    child = node->children->list[nc]; 
	    child->nparents--;
	    if (0==child->nparents) AddNode(Orphans,child);
	}
    }
}

void tree_close (void)
{
    FreeNodes(Tree,naxz);
    free(Orphans->list);
    free(Orphans);
}

static void psnap (float* p, float* q, int* iq) {
    int ia;
    float a2, a;

    a = cell_p2a(p);
    a2 = (a-a0)/da;
    ia = floor (a2); a2 -= ia;
    cell_snap (&a2, &ia, eps);

    if (ia < 0) {
	ia=0.; a2=0.; 
    } else if (ia > na-1 || (ia==na-1 && a2> 0.)) {
	ia=na-1; a2=0.;
    }

    a = a0+(ia+a2)*da;

    p[1] = sin(a);
    p[0] = -cos(a);

    *q = a2;
    *iq = ia;
}

