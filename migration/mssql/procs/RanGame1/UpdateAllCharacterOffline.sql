

CREATE PROCEDURE [dbo].[UpdateAllCharacterOffline]
AS
	DECLARE
		@error_var int, 
		@rowcount_var int
		
	SET NOCOUNT ON
	
	UPDATE ChaInfo WITH (UPDLOCK) 
	SET ChaOnline=0
	WHERE ChaOnline=1
	
    SET NOCOUNT OFF    


