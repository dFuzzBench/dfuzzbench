from debug_gym.gym.entities import Observation
from debug_gym.gym.tools.tool import EnvironmentTool
from debug_gym.gym.tools.toolbox import Toolbox

@Toolbox.register()
class ContextTool(EnvironmentTool):
    name: str = "get_context"
    description = (
        "Retrieves the source code context identified by the realistic retrieval. "
        "Allow you to understand code logic that are likely relevant to reaching the target line without manually viewing every file. "
    )

    # This tool takes no arguments from the agent.
    arguments = {}

    def use(self, environment) -> Observation:
        context = getattr(environment, "realistic_context", None)
        if not context:
            obs = "No realistic context available for this target."
        else:
            obs = context

        return Observation(self.name, obs)