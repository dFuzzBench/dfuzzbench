from debug_gym.gym.entities import Event, Observation
from debug_gym.gym.tools.tool import EnvironmentTool
from debug_gym.gym.tools.toolbox import Toolbox

@Toolbox.register()
class ProposeTool(EnvironmentTool):
    name: str = "propose_input"
    description = (
        "Propose the directed input to reach the target line. "
        "The input will be validated by running the fuzzing harness with it and checking if the target line is executed. "
        "The following is an example of your generated input string. "
        "It contains the required inputs to reach the target line in the code. "
        "Use '\n' to separate different lines. Your input should be wrapped by ``` as follows:\n"
        "```input1\ninput2\ninput3\n...```\n\n"
        "We will replace the 'EXAMPLE_INPUT' placeholder in the fuzzing harness with your proposed input string. "
        "If there are multiple inputs required by the fuzzing harness, use '\n\n' to separate different inputs as follows:\n"
        '```input1\ninput2\ninput3\n\ninput4\ninput5```\n'
        "Based on the example, the two inputs would be 'input1\ninput2\ninput3' and 'input4\ninput5'.\n\n"        
    )
    arguments = {
        "input": {
            "type": ["string"],
            "description": "The proposed directed input string to reach the target line, wrapped by ```.",
        },
    }

    def use(self, environment, input: str) -> Observation:
        input_data = environment.workspace.extract_input(input)
        if input_data is None:
            return Observation(
                self.name,
                f"Failed to parse the input string: {input}. Make sure your input is in correct format and wrapped by ```.",
            )

        input_mismatch_msg = environment.workspace.replace_harness_inputs(f"/src/testbed/{environment.harness_name}", input_data)
        success, output = environment.validate()
        # success, msg = environment.validate(input_data)

        if success:
            self.propose_success = True
            obs_output = output
        else:
            self.propose_success = False
            obs_output = f"{output} Try again." + (f" {input_mismatch_msg}" if input_mismatch_msg else "")
        
        self.queue_event(
            environment=environment,
            event=Event.PROPOSE_SUCCESS if self.propose_success else Event.PROPOSE_FAIL,
            proposed_input=input,
            message=obs_output,
        )
        return Observation(self.name, obs_output)
