import json,math,os,sys
HERE=sys.argv[1]
w=json.load(open(os.path.join(HERE,'wx.json'),encoding='utf-8'))['daily']
import datetime as D
def harg(tmin,tmax,lat,doy):
    phi=math.radians(lat);dr=1+.033*math.cos(2*math.pi/365*doy);dec=.409*math.sin(2*math.pi/365*doy-1.39)
    ws=math.acos(max(-1,min(1,-math.tan(phi)*math.tan(dec))))
    ra=24*60/math.pi*.082*dr*(ws*math.sin(phi)*math.sin(dec)+math.cos(phi)*math.cos(dec)*math.sin(ws))
    return max(0,.0023*((tmax+tmin)/2+17.8)*math.sqrt(tmax-tmin)*.408*ra)
xs=[];ys=[]
for t,e,a,b in zip(w['time'],w['et0_fao_evapotranspiration'],w['temperature_2m_max'],w['temperature_2m_min']):
    if not('04'<=t[5:7]<='09'):continue
    d=D.date.fromisoformat(t);xs.append(e);ys.append(harg(b,a,47.4979,d.timetuple().tm_yday))
n=len(xs);mx=sum(xs)/n;my=sum(ys)/n
r=sum((x-mx)*(y-my) for x,y in zip(xs,ys))/math.sqrt(sum((x-mx)**2 for x in xs)*sum((y-my)**2 for y in ys))
mae=sum(abs(x-y) for x,y in zip(xs,ys))/n;rmse=math.sqrt(sum((x-y)**2 for x,y in zip(xs,ys))/n)
print(dict(n=n,mx=mx,my=my,r=r,mae=mae,rmse=rmse,bias=my-mx,sumx=sum(xs),sumy=sum(ys)))
json.dump(dict(x=xs,y=ys,r=r,mae=mae,rmse=rmse,bias=my-mx,n=n),open(os.path.join(HERE,'harg.json'),'w'))
