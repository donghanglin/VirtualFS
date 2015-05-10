"""
Author: Donghang Lin
Date:   2015-05-10
"""

import re
import time

FUSEDATA = "/fusedata/fusedata."
BLOCKSIZE = 4096

# check superblock
print "--------------------check superblock--------------------\n"
f = open(FUSEDATA + '0', "r")
cont = f.read()
f.close()

superblock = re.split(r',', cont)
id = re.search(r'\d+', superblock[2])
if (id.group() != '20'):
	print "Device ID is wrong, it is not the targeted file system."
else:
	creationtime = re.search(r'\d+', superblock[0])
	f = open(FUSEDATA + '0', "w")
	change = False
	now = int(time.time())
	if (int(creationtime.group()) > now):
		change = True
		superblock[0] = "{creationTime: " + str(now)
		print "Creationtime is wrong, correct it to now (" + str(now) + ")"

	if (change):
		f.write(superblock[0] + "," + superblock[1] + "," + superblock[2] + "," + superblock[3] + \
			                    "," + superblock[4] + "," + superblock[5] + "," + superblock[6])
	else:
		f.write(cont)
		print "Superblock is correct."
	f.close()

	freelist = [['0' for col in range(400)] for row in range(25)]
	
	# check directories and files
	print "\n--------------------check directories and files--------------------\n"

	parentTable = {}
	parentTable['26'] = '26'

	def checkdir(block):
		global childToParentTable
		wrong = False
		f = open(FUSEDATA + str(block), "r")
		cont = f.read()
		f.close()
		dir = re.split(r',', cont)
		atime = re.search(r'\d+', dir[4])
		ctime = re.search(r'\d+', dir[5])
		mtime = re.search(r'\d+', dir[6])
		linkcount = re.search(r'\d+', dir[7])
		now = int(time.time())
		if (int(atime.group()) > int(now)):
			dir[4] = " atime:" + str(now)
			wrong = True
			print "Block " + str(block) + ": atime of this directory is wrong, correct it to now (" + \
				  str(now) + ")"
		if (int(ctime.group()) > int(now)):
			dir[5] = " ctime:" + str(now)
			wrong = True
			print "Block " + str(block) + ": ctime of this directory is wrong, correct it to now (" + \
				  str(now) + ")"
		if (int(mtime.group()) > int(now)):
			dir[6] = " mtime:" + str(now)
			wrong = True
			print "Block " + str(block) + ": mtime of this directory is wrong, correct it to now (" + \
				  str(now) + ")"

		makeup = ""
		isDot = False
		isDotdot = False
		files = []
		directories = []
		dictstart = cont.rindex("{")
		dictend = cont.index("}")
		dicts = re.split(r',', cont[dictstart + 1: dictend])

		for dirfile in range(len(dicts)):
			dot = re.search(r'd:\.:\d+', dicts[dirfile])
			dotdot = re.search(r'd:\.\.:\d+', dicts[dirfile])
			file = re.search(r'f:.+:\d+', dicts[dirfile])
			directory = re.search(r'd:.+:\d+', dicts[dirfile]) 
			if (dot != None):
				isDot = True
				dotblock = re.search(r'\d+', dot.group())
				if (dotblock.group() != str(block)):
					dicts[dirfile] = "d:.:" + str(block)
					wrong = True
					print "Block " + str(block) + ": the . directory's block is wrong, correct it to " + \
					      str(block)
			if (dotdot != None):
				isDotdot = True
				dotblock = re.search(r'\d+', dotdot.group())
				if (dotblock.group() != parentTable[str(block)]):
					dicts[dirfile] = "d:..:" + parentTable[str(block)]
					wrong = True
					print "Block " + str(block) + ": the .. directory's block is wrong, correct it to " + \
					      parentTable[str(block)]
			if (file != None):
				fileinode = re.findall(r'\d+', file.group())[-1]
				files.append(int(fileinode))
			if (dot == None and dotdot == None and directory!= None):
				directoryinode = re.findall(r'\d+', directory.group())[-1]
				directories.append(int(directoryinode))
				parentTable[directoryinode] = str(block)

		if (not isDot):
			makeup = makeup + "d:.:" + str(block) + ", "
			wrong = True
			print "Block " + str(block) + \
			      ": this directory doesn't contain . directory, add . directory to filename_to_inode_dict"
		if (not isDotdot):
			makeup = makeup + "d:..:" + parentTable[str(block)] + ", "
			wrong = True
			print "Block " + str(block) + \
			      ": this directory doesn't contain .. directory, add .. directory to filename_to_inode_dict"

		if (linkcount.group() != str(len(dir) - 8 + int(not isDot) + int(not isDotdot))):
			dir[7] = " linkcount:" + str(len(dir) - 8)
			wrong = True
			print "Block " + str(block) + ": linkcount of this directory is wrong, correct it to " + \
			      str(len(dir) - 8 + int(not isDot) + int(not isDotdot))

		if (wrong):
			f = open(FUSEDATA + str(block), "w")
			f.write(dir[0] + "," + dir[1] + "," + dir[2] + "," + dir[3] + ","+ dir[4] + ","+ dir[5] + \
				             "," + dir[6] + "," + dir[7] + ", filename_to_inode_dict: {" + makeup)
			for i in range(len(dicts) - 1):
				f.write(dicts[i] + ",")
			f.write(dicts[len(dicts) - 1] + "}}")
			f.close()
		else:
			print "Block " + str(block) + ": this directory is correct."

		for i in range(len(files)):
			checkfile(files[i])
		for i in range(len(directories)):
			checkdir(directories[i])

	def checkfile(block):
		wrong = False
		f = open(FUSEDATA + str(block), "r")
		cont = f.read()
		f.close()

		file = re.split(r',', cont)
		size = re.search(r'\d+', file[0])
		linkcount = re.search(r'\d+', file[4])
		atime = re.search(r'\d+', file[5])
		ctime = re.search(r'\d+', file[6])
		mtime = re.search(r'\d+', file[7])
		indirect_location = re.split(r' ', file[8])
		indirect = re.search(r'\d+', indirect_location[1])
		location = re.search(r'\d+', indirect_location[2])
		now = int(time.time())

		if (int(atime.group()) > int(now)):
			file[5] = " atime:" + str(now)
			wrong = True
			print "Block " + str(block) + ": atime of this file is wrong, correct it to now (" + \
				  str(now) + ")"
		if (int(ctime.group()) > int(now)):
			file[6] = " ctime:" + str(now)
			wrong = True
			print "Block " + str(block) + ": ctime of this file is wrong, correct it to now (" + \
				  str(now) + ")"
		if (int(mtime.group()) > int(now)):
			file[7] = " mtime:" + str(now)
			wrong = True
			print "Block " + str(block) + ": mtime of this file is wrong, correct it to now (" + \
				  str(now) + ")"

		arrayinfo = array(int(location.group()))
		arraynum = arrayinfo[0]
		arraylast = arrayinfo[1]

		if (arraynum == 0 and indirect.group() != '0'):
			if (arraylast != 0):
				f = open(FUSEDATA + location.group(), "w")
				f.write(BLOCKSIZE * "0")
				f.close()
				indirect_location[2] = "location:" + str(arraylast) + "}"
				print "Block " + str(block) + ": location of this file is wrong, correct it to " + \
				      str(arraylast) + ", free block " + location.group()
			indirect_location[1] = "indirect:0"
			wrong = True
			print "Block " + str(block) + ": indirect of this file is wrong, correct it to 0"

		if (arraynum != 0 and indirect.group() == '0'):
			indirect_location[1] = "indirect:1"
			wrong = True
			print "Block " + str(block) + ": indirect of this file is wrong, correct it to 1"

		if (arraynum == 0 and (int(size.group()) > BLOCKSIZE or (int(size.group()) < 0))):
			if (arraylast == 0):
				f = open(FUSEDATA + location.group(), "r")
			if (arraylast != 0):
				f = open(FUSEDATA + str(arraylast), "r")
			filecont = f.read()
			f.close()
			filelen = len(filecont)
			file[0] = "{size:" + str(filelen)
			wrong = True
			print "Block " + str(block) + ": size of this file is wrong, correct it to " + str(filelen)

		if (arraynum != 0 and (int(size.group()) > BLOCKSIZE * arraynum or \
			                                   int(size.group()) < BLOCKSIZE * (arraynum - 1))):
			f = open(FUSEDATA + str(arraylast), "r")
			filecont = f.read()
			f.close()
			filelen = len(filecont)
			file[0] = "{size:" + str(BLOCKSIZE * (arraynum - 1) + filelen)
			wrong = True
			print "Block " + str(block) + ": size of this file is wrong, correct it to " + \
			      str(BLOCKSIZE * (arraynum - 1) + filelen)

		if (wrong):
			f = open(FUSEDATA + str(block), "w")
			f.write(file[0] + ',' + file[1] + ',' + file[2] + ',' + file[3] + ',' + file[4] + ',' + \
				    file[5] + ',' + file[6] + ',' + file[7] + ', ' + indirect_location[1] + " " + indirect_location[2])
			f.close()
		else:
			print "Block " + str(block) + ": this file is correct."

	def array(block):
		f = open(FUSEDATA + str(block), "r")
		cont = f.read()
		f.close()
		num = re.findall(r'\d+', cont)
		comma = re.findall(r',', cont)
		if (len(num) == 1 and len(comma) == 0):
			return 0, int(num[-1])
		if (len(num) != len(comma) + 1):
			return 0, 0
		else:
			return len(num), int(num[-1])

	checkdir(26)

	# check freelist
	print "\n--------------------check freelist--------------------\n"
	for flist in range(25):
		f = open(FUSEDATA + str(flist + 1), "r")
		cont = f.read()
		f.close()
		blocks = re.findall(r'\d+', cont)
		for blockn in range(len(blocks)):
			i = int(blocks[blockn]) / 400
			j = int(blocks[blockn]) % 400
			freelist[i][j] = blocks[blockn]

	change = False
	for i in range(25):
		wrong = False
		for j in range(400):
			f = open(FUSEDATA + str(400 * i + j), "r")
			cont = f.read()
			f.close()
			zero = re.findall(r'0', cont)
			contlen = len(zero)
			if (freelist[i][j] == '0') and (contlen == BLOCKSIZE):
				wrong = True
				change = True
				freelist[i][j] = str(400 * i + j)
				print "Block " + freelist[i][j] + " is false taken, add it to freelist"
			if (freelist[i][j] != '0') and (contlen != BLOCKSIZE):
				wrong = True
				change = True
				freelist[i][j] = '0'
				print "Block " + str(400 * i + j) + " is false empty, delete it from freelist"
		if (wrong == True):
			f = open(FUSEDATA + str(i + 1), "w")
			isFirst = False
			for blocks in range(400):
				if (freelist[i][blocks] != '0' and isFirst):
					f.write(", " + freelist[i][blocks])
				if (freelist[i][blocks] != '0' and not isFirst):
					isFirst = True
					f.write(freelist[i][blocks])
			f.close()
	if (not change):
		print "Freelist is correct."





