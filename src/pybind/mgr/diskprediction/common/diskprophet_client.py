# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""The Python implementation of the gRPC route guide client."""

from __future__ import print_function

import random

import grpc
import json
import client_pb2 as mainServer_pb2
import client_pb2_grpc as mainServer_pb2_grpc


def run():
    host = 'api.federator.ai'
    port = 31400

    auth = [
        ('account', 'test1'),
        ('password', 'VuS9jZ8uVbhV3vC5')]

    cert_file = 'server.crt'
    with open(cert_file, 'rb') as f:
        trusted_certs = f.read()
    creds = grpc.ssl_channel_credentials(root_certificates=trusted_certs)

    channel = grpc.secure_channel('{}:{}'.format(host, port), creds, options=(('grpc.ssl_target_name_override', 'api.federator.ai',), ('grpc.default_authority', 'api.federator.ai'),))

    # channel = grpc.insecure_channel('127.0.0.1:50050')  // LOCAL TEST
# ------------------------------------
    stub = mainServer_pb2_grpc.GeneralStub(channel)
    pp = stub.GeneralHeartbeat(mainServer_pb2.Empty(),metadata=auth)
    print(pp)
# ------------------------------------
    stubAccout = mainServer_pb2_grpc.AccountStub(channel)
    ppAccount1 = stubAccout.AccountHeartbeat(mainServer_pb2.Empty())
    print(ppAccount1)
# ------------------------------------
    stubDP = mainServer_pb2_grpc.DiskprophetStub(channel)
    ppDP = stubDP.DPHeartbeat(mainServer_pb2.Empty(),metadata=auth)
    print(ppDP)

    ppDP3 = stubDP.DPGetPhysicalDisks(mainServer_pb2.DPGetPhysicalDisksInput(),metadata=auth)
    # print(ppDP3)
    my_json = ppDP3.data.decode('utf8').replace("'", '"')
    d = json.loads(ppDP3.data)
    print(d['results'][0]['series'][0]['columns'])
    print(d['results'][0]['series'][0]['values'][0])
    print("------------------------------------------------")
    print("measurement:" + d['results'][0]['series'][0]['name'])
    print("column:" + d['results'][0]['series'][0]['columns'][1])
    print("value:" +d['results'][0]['series'][0]['values'][0][1])
    print("------------------------------------------------")

    ppDP4 = stubDP.DPGetDisksPrediction(mainServer_pb2.DPGetDisksPredictionInput(physicalDiskIds="5000039411b04a35"),metadata=auth)
    # print(ppDP4) 
    my_json2 = ppDP4.data.decode('utf8').replace("'", '"')
    d2 = json.loads(ppDP4.data)
    print(d2['results'][0]['series'][0]['columns'])
    print(d2['results'][0]['series'][0]['values'][0])
    print("------------------------------------------------")
    print("measurement:" + d2['results'][0]['series'][0]['name'])
    print("column:" + d2['results'][0]['series'][0]['columns'][1])
    print("value:" +d2['results'][0]['series'][0]['values'][0][1])
    print("------------------------------------------------")
# ------------------------------------
    stubCollection = mainServer_pb2_grpc.CollectionStub(channel)
    ppCollection = stubCollection.CollectionHeartbeat(mainServer_pb2.Empty(),metadata=auth)
    print(ppCollection)

    words=[
        'cpu,cpu=cpu-total,agenthost=prophetstor-hahahaPYTHON,agent_version=1.5.0-unknown,agenthost_domain_id=2490d067390949be6b9077919c8c39f1 usage_nice=0,usage_irq=0,usage_steal=0,usage_guest=0,usage_softirq=0.05056890012639114,usage_guest_nice=0,usage_user=9.86093552465892,usage_system=4.070796460179661,usage_idle=85.46144121364932,usage_iowait=0.5562579013914524 1526971984877112345',
        'ntnx_volume_group,agenthost_domain_id=2490d067390949be6b9077919c8c39f1,agenthost=prophetstor-winlinu,agent_version=1.5.0-unknown,vmdisk_uuid=45316226-b883-426b-90e7-6bb6fe52f3b3,container_uuid=191dee9e-80ed-4fad-927a-8a3ff814a917,uuid=1483743f-e05f-4dd1-9b6c-dfd89ef0904b,name=VG-1 vmdisk_size_mb=204800i,container_id=8i,vm_uuid="0a9b5641-b0f5-4f94-8887-1c6e5a701f63" 12345678'
    ]
    try:
        ppCollection1 = stubCollection.PostMetrics(mainServer_pb2.PostMetricsInput(points=words),metadata=auth)
        # ppCollection1 = stubCollection.PostMetrics(mainServer_pb2.PostMetricsInput(points=words))    # error test
    except grpc.RpcError as e:
            # print(e.details())
            status_code = e.code()
            print(status_code.name)
            print(status_code.value)
    else:
        print(ppCollection1)
        print(ppCollection1.status)
        print(ppCollection1.message)
        # print("message:" +  ppCollection1.message)
    
    print("------------------------------------------------")

    cmdArray = [
        "merge(VMDisk:VMDisk{name:'4', domainId:'7'}) set VMDisk.time=123555",
        "merge(VMDisk:VMDisk{name:'5', domainId:'8'}) set VMDisk.time=123555",
        "merge(VMDisk:VMDisk{name:'6', domainId:'9'}) set VMDisk.time=123555"
    ]
    ppCollection4 = stubCollection.PostDBRelay(mainServer_pb2.PostDBRelayInput(cmds=cmdArray),metadata=auth)
    print(ppCollection4)
    print(ppCollection4.status)
    print(ppCollection4.message)
# ------------------------------------
#    print("-------------- GetFeature --------------")
#   guide_get_feature(stub)
#    print("-------------- ListFeatures --------------")
#    guide_list_features(stub)
#    print("-------------- RecordRoute --------------")
#    guide_record_route(stub)
#    print("-------------- RouteChat --------------")
#    guide_route_chat(stub)


if __name__ == '__main__':
    run()
