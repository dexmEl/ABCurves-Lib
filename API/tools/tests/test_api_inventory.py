"""Parser and direction checks against real current and frozen ABI headers."""
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('audit',ROOT/'API/tools/audit_application_io.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)

class InventoryTests(unittest.TestCase):
    def test_all_original_and_current_exports(self):
        old,_=audit.parse_header((ROOT/'tests/baselines/abi-1.3/abcurves.h').read_text())
        current,fields=audit.parse_header((ROOT/audit.HEADER).read_text())
        self.assertEqual(len(old),50);self.assertEqual(len(current),55)
        self.assertEqual(len({o['name'] for o in current}),55)
        self.assertTrue({o['name'] for o in old}<={o['name'] for o in current})
        for original in old:self.assertIn(original,current)
        for o in current:
            self.assertIn(o['return_type'],('abc_status','const char *'))
            self.assertNotRegex(o['return_type'],r'#|typedef|\benum\b|;|\{|\}')
        by_name={o['name']:o for o in current}
        self.assertEqual(by_name['abc_get_version']['return_type'],'abc_status')
        self.assertEqual(by_name['abc_model_load']['parameters'],[{'name':'utf8_directory','type':'const char *'},{'name':'out','type':'abc_model_handle *'}])
        self.assertEqual(by_name['abc_planner_create']['parameters'][1]['type'],'const abc_planner_options *')
        self.assertEqual(sum(map(len,fields.values())),169)
        for original,rows in audit.parse_header((ROOT/'tests/baselines/abi-1.3/abcurves.h').read_text())[1].items():self.assertEqual(fields[original],rows)

    def test_preprocessor_cannot_become_a_function(self):
        header=(ROOT/audit.HEADER).read_text()
        expected=audit.parse_header(header)
        prefix='#define ABC_API __declspec(dllexport)\n#define EXAMPLE(x) \\\n typedef enum bogus { x } abc_bad;\n/* ABC_API wrong ABC_CALL abc_fake(void); */\n'
        self.assertEqual(audit.parse_header(prefix+header),expected)

    def test_every_parameter_has_a_valid_direction(self):
        ops,fields=audit.parse_header((ROOT/audit.HEADER).read_text())
        for op in ops:
            for p in op['parameters']:
                role=audit.parameter_contract(op['name'][4:],p['name'],p['type'],fields)
                self.assertIn(role['direction'],('in','out','inout'))
                self.assertTrue(role['validity'])
                if '_handle' in p['type']:
                    expected='output_handle_address' if '*' in p['type'] else 'input_handle'
                    self.assertEqual(role['category'],expected)
                    if '*' in p['type']:self.assertIn('no pre-existing live token required',role['validity'])
        role=lambda short,name,typ:audit.parameter_contract(short,name,typ,fields)
        self.assertEqual(role('get_version','out','abc_version_info *')['direction'],'inout')
        self.assertEqual(role('planner_options_init','out','abc_planner_options *')['direction'],'out')
        self.assertEqual(role('planner_advance','output','abc_sample *')['category'],'output_buffer')
        self.assertEqual(role('renderer_render_samples','samples','const abc_sample *')['category'],'input_buffer')
        self.assertEqual(role('renderer_render_samples','previous_position','abc_vec2 *')['direction'],'inout')
        # Source-reviewed matrix: every non-input parameter in each export.
        expected={
            'get_version':{'out':'inout'},'model_load':{'out':'out'},'model_load_embedded':{'out':'out'},'model_get_info':{'out':'inout'},
            'model_get_tensor_info':{'out':'inout'},'planner_options_init':{'out':'out'},
            'planner_create':{'out':'out'},'planner_get_model_info':{'out':'inout'},
            'planner_get_state':{'out':'inout'},'planner_required_capacity':{'out':'out'},
            'planner_advance':{'output':'out','written':'out'},'planner_generate_sample':{'output':'out'},
            'renderer_create':{'out':'out'},'renderer_create_embedded':{'out':'out'},'renderer_step':{'out':'out'},
            'renderer_step_batch':{'output':'out','written':'out'},
            'renderer_render_samples':{'previous_position':'inout','output':'out','written':'out'},
            'observation_options_init':{'out':'out'},'observation_create':{'out':'out'},
            'observation_submit':{'resolved_target':'out'},'observation_get_diagnostics':{'out':'inout'},
            'observation_layout':{'options_size':'out','observation_size':'out','diagnostics_size':'out'},
            'prepare_history':{'prepared':'out','filtered_anchor':'out'},'segment_options_init':{'out':'out'},
            'segment_layout':{'options_size':'out','metadata_size':'out','diagnostics_size':'out'},
            'segment_create':{'out':'out'},'segment_required_capacity':{'out':'out'},
            'segment_next':{'output':'out','written':'out','metadata':'inout'},
            'segment_get_metadata':{'out':'inout'},'segment_get_diagnostics':{'out':'inout'},
            'segment_recovery_layout':{'diagnostics_size':'out'},'segment_get_recovery_diagnostics':{'out':'inout'},
            'segment_state_layout':{'state_size':'out'},'segment_get_state':{'out':'inout'},
            'segment_render':{'output':'out','written':'out'},
        }
        for op in ops:
            short=op['name'][4:]
            actual={p['name']:role(short,p['name'],p['type'])['direction'] for p in op['parameters'] if role(short,p['name'],p['type'])['direction']!='in'}
            self.assertEqual(actual,expected.get(short,{}),short)

    def test_generated_json_and_field_coverage(self):
        original_doc,original_out=audit.DOC,audit.OUT
        with tempfile.TemporaryDirectory() as directory:
            audit.DOC=audit.OUT=Path(directory)
            try:
                counts=audit.inventory()
                inputs=json.loads((audit.DOC/'application_inputs.json').read_text())
                outputs=json.loads((audit.DOC/'application_outputs.json').read_text())
                self.assertEqual(counts['c_exports'],55)
                for o in inputs['c_operations']:
                    self.assertNotRegex(o['signature'],r'#|typedef|\benum\b|\{|\}')
                expected={s+'.'+n for s,rows in audit.fields().items() for n,_ in rows if s!='abc_target_observation'}
                self.assertTrue(expected<={r['name'] for r in outputs['outputs']})
                for r in inputs['inputs']:
                    if r.get('category')=='output_handle_address':
                        self.assertNotIn('Live correctly typed token',r['description'])
                        self.assertIn('no pre-existing live token',r['description'])
            finally:audit.DOC,audit.OUT=original_doc,original_out

if __name__=='__main__':unittest.main()
