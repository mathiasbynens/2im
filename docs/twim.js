(zis=>{"use strict";let k=a=>new Int32Array(a),m=(a,f,c)=>{a.set(f,c)},r=a=>a.length-1,t=Math,u=256,v=u*u,w=v*v,x=w*u,y=x*u,A=t.floor,B=t.round,C=(a,f)=>{for(let c=3*a[r(a)]-3;0<=c;c-=3)f(a[c],a[c+1],a[c+2])},D=a=>{if(a)throw 4};let E=4*v,F=2*u,G=k(F),H=k(F);for(let a=0;a<F;++a){let f=t.PI*a/F;G[a]=B(E*t.sin(f));H[a]=B(E*t.cos(f))}let I,J,K,L=k(7),M=k(7),N=a=>{let f=J+1,c=-1,g=I+1,d=-1;C(a,(e,h,n)=>{f=f<e?f:e;c=c>e?c:e;g=g<h?g:h;d=d>n?d:n});D(0>c);a=d-g;var b=c+1-f;a=a*a+b*b;for(b=0;6>b&&!(a>=L[b]);++b);return b};let O,P,Q,R,S;let T=0,U=0,V=0,W=0,X=(a,f)=>{let c=G[f],g=H[f],d=w,b=-w;C(a,(e,h,n)=>{h=g*e+c*h;e=g*e+c*n-c;d=d<h?d:h;b=b>e?b:e});D(b<d);U=d;V=b;for(W=E;;)if(T=(b-d)/W|0,25<T)W+=W/16|0;else break};let aa=(a,f,c,g,d)=>{let b=G[f],e=H[f],h=0,n=0;b?(c=2*c+b,e*=2,b*=2,C(a,(p,l,q)=>{let z=(c-p*e)/b|0;z<q&&m(g,[p,z>l?z:l,q],3*h++);z>l&&m(d,[p,l,z<q?z:q],3*n++)})):C(a,(p,l,q)=>{p*e>=c?m(g,[p,l,q],3*h++):m(d,[p,l,q],3*n++)});g[r(g)]=h;d[r(d)]=n};let Y=a=>{if(2>a)return 0;{P=A(P/a);let f=A((R-O)/P);D(0>f||f>=a);a=f}O+=a*P;for(P*=a+1-a;;){if(A(O/x)!==A((O+P-1)/x)){if(P>=w)break;P=(y-O)%w}R=R%x*u+(S[Q++]|0);P=P%x*u;O=O%x*u}return a},Z=a=>(255*Y(a)+a-2)/(a-1)|0,ba=(a,f,c,g)=>{if(0==Y(2)){let n=[Z(K),Z(K),Z(K),u-1];C(a,(p,l,q)=>{for(;l<q;++l)g.set(n,4*(p*c+l))})}else{var d=N(a),b=3*a[r(a)]+1,e=k(b);b=k(b);d=1<<M[d];var h=F/d;d=Y(d)*h;X(a,d);h=Y(T);aa(a,d,1<T?U+((V-U-(T-1)*W)/2|0)+W*h:(V+U)/2|0,e,b);f.push(e,b)}},ca=(a,f,c)=>{S=a;R=Q=O=0;P=y-1;for(a=0;6>a;++a)R=R*u+(S[Q++]|0);I=f;J=c;a=Y(8500);var g=a%4;a=a/4|0;var d=2+a%5;a=a/5|0;var b=10**(3-a%5/5)|0;a=a/5|0;var e=a%5;a=a/5|0;d=(I*I+J*J)*d*d;for(let h=0;7>h;++h)L[h]=d/36,d=40*d/b|0;g=9-g;for(b=0;7>b;++b)M[b]=t.max(g-b-(b*e/2|0),0);K=1+(4+(a&3)<<(a>>2));e=k(3*c+1);e[r(e)]=c;for(a=0;a<c;++a)m(e,[a,0,f],3*a);a=new Uint8ClampedArray(4*f*c);e=[e];for(g=0;g<=r(e);)for(b=r(e);g<=b;++g)ba(e[g],e,f,a);return new ImageData(a,f,c)};zis.decode2im=ca})(window);