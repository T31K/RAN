

CREATE PROCEDURE [dbo].[UpdateChaOnline]
	@nChaNum int,
	@nChaOnline int
AS	
	DECLARE
		@error_var int, 
		@rowcount_var int
		
	SET NOCOUNT ON
	
	UPDATE ChaInfo
	SET ChaOnline=@nChaOnline 
	WHERE ChaNum=@nChaNum
	
    SET NOCOUNT OFF    


