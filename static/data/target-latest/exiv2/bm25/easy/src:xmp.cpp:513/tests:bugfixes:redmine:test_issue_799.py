# -*- coding: utf-8 -*-

from system_tests import DeleteFiles, CopyFiles, CaseMeta, path


@DeleteFiles("$xmpfile")
@CopyFiles("$data_path/exiv2-empty.jpg")
class WrongXmpTypeForNestedXmpKeys(metaclass=CaseMeta):

    url = "http://dev.exiv2.org/issues/$num"

    num = 799
    cmdfile = path("$data_path/bug$num.cmd")

    filename_common = path("$data_path/exiv2-empty_copy")
    filename = "$filename_common.jpg"
    xmpfile = "$filename_common.xmp"

    commands = [
        "$exiv2 -v -m $cmdfile $filename",
        "$exiv2 -v -pa $filename",
        "$exiv2 -f -eX $filename",
    ]

    def post_tests_hook(self):
        with open(self.xmpfile, "r", encoding='utf-8') as xmp_file:
            self.assertMultiLineEqual(self.xmp_packet, xmp_file.read(-1))

    xmp_packet = """<?xpacket begin="\ufeff" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="XMP Core 4.4.0-Exiv2">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about=""
    xmlns:MP="http://ns.microsoft.com/photo/1.2/"
    xmlns:MPRI="http://ns.microsoft.com/photo/1.2/t/RegionInfo#"
    xmlns:MPReg="http://ns.microsoft.com/photo/1.2/t/Region#"
    xmlns:mwg-rs="http://www.metadataworkinggroup.com/schemas/regions/"
    xmlns:stDim="http://ns.adobe.com/xap/1.0/sType/Dimensions#"
    xmlns:stArea="http://ns.adobe.com/xmp/sType/Area#">
   <MP:RegionInfo rdf:parseType="Resource">
    <MPRI:Regions>
     <rdf:Bag>
      <rdf:li
       MPReg:Rectangle="0.11, 0.22, 0.33, 0.44"
       MPReg:PersonDisplayName="Baby Gnu"/>
     </rdf:Bag>
    </MPRI:Regions>
   </MP:RegionInfo>
   <mwg-rs:Regions rdf:parseType="Resource">
    <mwg-rs:AppliedToDimensions
     stDim:w="1600"
     stDim:h="800"
     stDim:unit="pixel"/>
    <mwg-rs:RegionList>
     <rdf:Bag>
      <rdf:li>
       <rdf:Description
        mwg-rs:Name="Baby Gnu"
        mwg-rs:Type="Face