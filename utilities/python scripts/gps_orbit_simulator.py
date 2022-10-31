import requests
import datetime
from datetime import datetime
from datetime import timezone
import ephem
import time
import serial
# https://www.geeksforgeeks.org/python-datetime-to-integer-timestamp/#:~:text=Example%201%3A%20Integer%20timestamp%20of%20the%20current%20date%20and%20time&text=Convert%20the%20DateTime%20object%20into,the%20integer%20timestamp%20in%20seconds.

# https://rhodesmill.org/pyephem/quick.html
NORAD_CATNR = "42784"
ser =serial.Serial('COM7')

def get_tle(NORAD_CATNR):
	request_str = "https://celestrak.org/NORAD/elements/gp.php?CATNR="+NORAD_CATNR+"&FORMAT=TLE"
	response = requests.get(request_str)
	response_text = response.text
	TLE = response_text.splitlines()
	return TLE

####################COMPUTE WITH NOW DATE############
def now_lat_lon_alt(tle_rec):
	now = datetime.now() 
	print("now =", now)
	tle_rec.compute(now)
	return tle_rec.sublat,tle_rec.sublong,tle_rec.elevation #  DMS


def fixed_date_lat_lon_alt(yy,mon,dd,hh,mm,ss):
	d = datetime(yy, mon, dd, hh, mm, ss) # set date : year month date, hours minutes seconds
	tle_rec.compute(d)
	print("now =", d.timestamp())
	return tle_rec.sublat,tle_rec.sublong,tle_rec.elevation #  DMS

def fixed_date_with_dt_lat_lon_alt(yy,mon,dd,hh,mm,ss,dt):
	d = datetime(yy, mon, dd, hh, mm, ss)
	d_timestamp = d.timestamp() # convert to timestamp
	d_timestamp = d_timestamp + dt # add dt in seconds to timestamp
	print("now =", d_timestamp)
	time_object = datetime.fromtimestamp(d_timestamp) # convert timestamp back into dateandtime
	tle_rec.compute(time_object) # compute with new time object
	return tle_rec.sublat,tle_rec.sublong,tle_rec.elevation #  DMS

def datetime_to_utc(yy,mon,dd,hh,mm,ss):
	# for RMC message
	utc_string = str(hh)+str(mm)+str(ss)+".00"
	date_string = str(dd)+str(mon)+str(yy)
	return utc_string,date_string

def compile_RMC(yy,mon,dd,hh,mm,ss,lat,lon,alt):
	# reference
	# https://orolia.com/manuals/VSP/Content/NC_and_SS/Com/Topics/APPENDIX/NMEA_RMCmess.htm
	utc_string,date_string = datetime_to_utc(yy,mon,dd,hh,mm,ss)
	Status = "A"
	N_or_S = None
	E_or_W = None

	lat = str(lat). split(":") # convert to string and split by :
	if float(lat[0])>=0:
		N_or_S = 'N'
	elif float(lat[0])<0:
		N_or_S = 'S'
		#lat[0] = str(abs(int(lat[0]) ))
		lat[0]=lat[0].replace('-','')

	lat_min = float(lat[1])
	lat_sec = float(lat[2])
	lat_mmdotmm = lat_min + lat_sec/60 # Latitude (in DDMM.MMM format)
	lat_mmdotmm = "{:.3f}".format(round(lat_mmdotmm, 3))
	nmea_lat_string = lat[0]+lat_mmdotmm
	#print(nmea_lat_string)

	lon = str(lon). split(":") # convert to string and split by :
	if float(lon[0])>=0:
		E_or_W = 'E'
	elif float(lon[0])<0:
		E_or_W = 'W'
		#lon[0] = str(abs(int(lon[0]) ))
		lon[0]=lon[0].replace('-','')

	lon_min = float(lon[1])
	lon_sec = float(lon[2])
	lon_mmdotmm = lon_min + lon_sec/60 # Latitude (in DDMM.MMM format)
	lon_mmdotmm = "{:.3f}".format(round(lon_mmdotmm, 3))
	nmea_lon_string = lon[0]+lon_mmdotmm
	#print(nmea_lon_string)

	full_RMC_string = "$GPRMC,"+utc_string+","+Status+","+nmea_lat_string+","+N_or_S+","+nmea_lon_string+','+E_or_W+",,"+date_string+",,A,*6A"
	print(full_RMC_string)
	ser.write(str.encode(full_RMC_string))
	ser.write(bytearray(b'\x0a')) # new line byte 0x0a


TLE = get_tle(NORAD_CATNR)
tle_rec = ephem.readtle(TLE[0],TLE[1],TLE[2]) # feed tle lines to create compute object


"""

print ("-------- NOW LAT LON ALT ")
lat,lon,alt = now_lat_lon_alt(tle_rec)
print(lat,lon,alt)




print ("fixed date ------ ")
lat,lon,alt = fixed_date_lat_lon_alt(2022,10,10,0,0,0)
print(lat,lon,alt)
compile_RMC(2022,10,10,0,0,0,lat,lon,alt)



print ("fixed date with dt ------ ")
lat,lon,alt = fixed_date_with_dt_lat_lon_alt(2022,10,10,0,0,0,3600)
print(lat,lon,alt)
compile_RMC(2022,10,10,0,0,0,lat,lon,alt)




compile_RMC(2022,10,10,12,24,0,"-01:40:03.8","-05:47:57.0","486763.25")


#d = datetime(2022, 10, 21, 0, 0, 0)
#utc_timestamp,date_string = datetime_to_utc(2022, 10, 21, 12, 13, 45)
#print(utc_timestamp,date_string)
"""

SIMULATION_DURATION = 100 # seconds  
SIMULATION_dt = 1 #  set manualy so that duration/dt is integer 
ACCELERATION_FACTOR = 5 # should be integer


simulation_time =  0
sim_iterations = int(SIMULATION_DURATION/SIMULATION_dt)

starting_date = datetime(2022, 10, 10, 0, 0, 0)
starting_timestamp = starting_date.timestamp()
real_timestamp = starting_date.timestamp() # convert to timestamp


while real_timestamp<= starting_timestamp+SIMULATION_DURATION:
	simulation_time = simulation_time + SIMULATION_dt
	real_timestamp = real_timestamp + SIMULATION_dt*ACCELERATION_FACTOR
	real_datetime_object = datetime.fromtimestamp(real_timestamp)

	current_date = datetime.date(real_datetime_object)
	current_time = datetime.time(real_datetime_object)
	yy = current_date.year
	mon = current_date.month
	dd = current_date.day
	hh = current_time.hour
	mm = current_time.minute
	ss = current_time.second
	lat,lon,alt = fixed_date_lat_lon_alt(yy,mon,dd,hh,mm,ss)
	
	print("Simulation time ="+str(simulation_time))
	#print("Real time ="+str(real_timestamp))
	print("current time "+str(yy)+"y "+str(mon)+" mon "+str(dd)+" day "+str(hh)+" h "+str(mm)+" min "+str(ss)+" s")
	compile_RMC(yy,mon,dd,hh,mm,ss,lat,lon,alt)
	time.sleep(SIMULATION_dt)