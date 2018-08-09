from __future__ import absolute_import
import datetime

from .. import BaseAgent
from ...common.clusterdata import ClusterAPI

PREDICTION_FILE = '/var/tmp/disk_prediction.json'

TIME_DAYS = 24*60*60
TIME_WEEK = TIME_DAYS * 7


class PredictionAgent(BaseAgent):

    measurement = 'sai_disk_prediction'

    @staticmethod
    def _get_disk_type(is_ssd, vendor, model):
        """ return type:
            0: "Unknown", 1: "HDD",
            2: "SSD",     3: "SSD NVME",
            4: "SSD SAS", 5: "SSD SATA",
            6: "HDD SAS", 7: "HDD SATA"
        """
        if is_ssd:
            if vendor:
                disk_type = 4
            elif model:
                disk_type = 5
            else:
                disk_type = 2
        else:
            if vendor:
                disk_type = 6
            elif model:
                disk_type = 7
            else:
                disk_type = 1
        return disk_type

    def _store_prediction_result(self, result):
        self._module_inst._prediction_result = result

    def _parse_prediction_data(self, host_domain_id, disk_domain_id):
        result = {}
        try:
            query_info = self._client.query_info(
                host_domain_id, disk_domain_id, 'sai_disk_prediction')
            status_code = query_info.status_code
            if status_code == 200:
                result = query_info.json()
            else:
                resp = query_info.json()
                if resp.get('error'):
                    self._logger.error(str(resp['error']))
        except Exception as e:
            self._logger.error(str(e))
        return result

    @staticmethod
    def _convert_timestamp(predicted_timestamp, life_expectancy_day):
        """

        :param predicted_timestamp: unit is nanoseconds
        :param life_expectancy_day: unit is seconds
        :return:
            date format '%Y-%m-%d' ex. 2018-01-01
        """
        return datetime.datetime.fromtimestamp(
            predicted_timestamp / (1000 ** 3) + life_expectancy_day).strftime('%Y-%m-%d')

    def _fetch_prediction_result(self):
        obj_api = ClusterAPI(self._module_inst)
        cluster_id = obj_api.get_cluster_id()

        result = {}
        osds = obj_api.get_osds()
        for osd in osds:
            osd_id = osd.get('osd')
            if osd_id is None:
                continue
            if not osd.get('in'):
                continue
            osds_meta = obj_api.get_osd_metadata(osd_id)
            if not osds_meta:
                continue
            osds_smart = obj_api.get_osd_smart(osd_id)
            if not osds_smart:
                continue

            hostname = osds_meta.get('hostname', 'None')
            host_domain_id = '%s_%s' % (cluster_id, hostname)

            for dev_name, s_val in osds_smart.iteritems():
                is_ssd = True if s_val.get('rotation_rate') == 0 else False
                vendor = s_val.get('vendor', '')
                model = s_val.get('model_name', '')
                disk_type = self._get_disk_type(is_ssd, vendor, model)
                serial_number = s_val.get('serial_number')
                wwn = s_val.get('wwn', {})
                wwpn = ''
                if wwn:
                    wwpn = '%06X%X' % (wwn.get('oui', 0), wwn.get('id', 0))
                    for k in wwn.keys():
                        if k in ['naa', 't10', 'eui', 'iqn']:
                            wwpn = ('%X%s' % (wwn[k], wwpn)).lower()
                            break

                tmp = {}
                if wwpn:
                    tmp['disk_domain_id'] = dev_name
                    tmp['disk_wwn'] = wwpn
                    if serial_number:
                        tmp['serial_number'] = serial_number
                    else:
                        tmp['serial_number'] = wwpn
                elif serial_number:
                    tmp['disk_domain_id'] = dev_name
                    tmp['serial_number'] = serial_number
                    if wwpn:
                        tmp['disk_wwn'] = wwpn
                    else:
                        tmp['disk_wwn'] = serial_number
                else:
                    tmp['disk_domain_id'] = dev_name
                    tmp['disk_wwn'] = dev_name
                    tmp['serial_number'] = dev_name

                if s_val.get('smart_status', {}).get('passed'):
                    tmp['smart_health_status'] = 'PASSED'
                else:
                    tmp['smart_health_status'] = 'FAILED'

                tmp['sata_version'] = s_val.get('sata_version', {}).get('string', '')
                tmp['sector_size'] = str(s_val.get('logical_block_size', ''))
                try:
                    if isinstance(s_val.get('user_capacity'), dict):
                        user_capacity = \
                            s_val['user_capacity'].get('bytes', {}).get('n', 0)
                    else:
                        user_capacity = s_val.get('user_capacity', 0)
                except ValueError:
                    user_capacity = 0
                disk_info = {
                    'disk_name': dev_name,
                    'disk_type': str(disk_type),
                    'disk_status': '1',
                    'disk_wwn': tmp['disk_wwn'],
                    'dp_disk_idd': tmp['disk_domain_id'],
                    'serial_number': tmp['serial_number'],
                    'vendor': vendor,
                    'sata_version': tmp['sata_version'],
                    'smart_healthStatus': tmp['smart_health_status'],
                    'sector_size': tmp['sector_size'],
                    'size': str(user_capacity),
                    'prediction': self._parse_prediction_data(
                        host_domain_id, tmp['disk_domain_id'])
                }
                # Update osd life-expectancy
                dev_id = ''
                predicted = None
                life_expectancy_day_min = None
                life_expectancy_day_max = None
                devs_info = obj_api.get_osd_device_id(osd_id)
                if disk_info.get('prediction', {}).get('predicted'):
                    predicted = int(disk_info['prediction']['predicted'])
                if disk_info.get('prediction', {}).get('near_failure'):
                    if disk_info['prediction']['near_failure'].lower() == 'good':
                        life_expectancy_day_min = (TIME_WEEK * 6) + TIME_DAYS
                        life_expectancy_day_max = None
                    elif disk_info['prediction']['near_failure'].lower() == 'warning':
                        life_expectancy_day_min = (TIME_WEEK * 2)
                        life_expectancy_day_max = (TIME_WEEK * 6) - TIME_DAYS
                    elif disk_info['prediction']['near_failure'].lower() == 'bad':
                        life_expectancy_day_min = 0
                        life_expectancy_day_max = (TIME_WEEK * 2) - TIME_DAYS
                    else:
                        # Near failure state is unknown.
                        predicted = None
                        life_expectancy_day_min = None
                        life_expectancy_day_max = None
                check_dev_id = ''
                if model:
                    check_dev_id += str(model).upper()
                if serial_number:
                    check_dev_id += (' %s' % serial_number).upper()
                check_dev_id = check_dev_id.replace(' ', '_')
                for dev_n, dev_info in devs_info.iteritems():
                    if dev_info.get('dev_id', '').find(check_dev_id) >= 0:
                        dev_id = dev_info['dev_id']
                        break

                if predicted and dev_id and life_expectancy_day_min:
                    from_date = None
                    to_date = None
                    try:
                        if life_expectancy_day_min:
                            from_date = self._convert_timestamp(predicted, life_expectancy_day_min)

                        if life_expectancy_day_max:
                            to_date = self._convert_timestamp(predicted, life_expectancy_day_max)

                        obj_api.set_device_life_expectancy(dev_id, from_date, to_date)
                        self._logger.info(
                            'succeed to set device {} life expectancy from: {}, to: {}'.format(
                                dev_id, from_date, to_date))
                    except Exception as e:
                        self._logger.error(
                            'failed to set device {} life expectancy from: {}, to: {}, {}'.format(
                                dev_id, from_date, to_date, str(e)))
                else:
                    if dev_id:
                        obj_api.reset_device_life_expectancy(dev_id)
                if dev_id:
                    result[dev_id] = disk_info

        return result

    def run(self):
        result = self._fetch_prediction_result()
        if result:
            self._store_prediction_result(result)
